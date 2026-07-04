#include "BrowserSearchIndex.h"

namespace conduit
{

BrowserSearchIndex::BrowserSearchIndex (juce::ThreadPool& workerToUse,
                                        Dispatcher dispatcherToUse)
    : worker (workerToUse),
      dispatcher (dispatcherToUse != nullptr
                      ? std::move (dispatcherToUse)
                      : Dispatcher ([] (std::function<void()> fn)
                        { juce::MessageManager::callAsync (std::move (fn)); }))
{
}

BrowserSearchIndex::~BrowserSearchIndex()
{
    // Laufende Jobs arbeiten nur auf kopierten Daten — das Alive-Flag
    // verhindert, dass ihr callAsync-Ergebnis ins Leere schreibt
    alive->store (false);
}

void BrowserSearchIndex::rebuildAsync (std::vector<Source> sources)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto jobGeneration = ++generation;

    worker.addJob ([this, jobGeneration, aliveFlag = alive,
                    captured = std::move (sources)]
    {
        // [Pool-Thread] — nur kopierte Daten anfassen
        auto built = std::make_shared<std::vector<Entry>>();
        built->reserve (captured.size());

        for (const auto& source : captured)
        {
            Entry entry;
            entry.itemId   = source.itemId;
            entry.haystack = (source.name + " " + source.category + " "
                              + source.tags + " " + source.itemId).toLowerCase();
            built->push_back (std::move (entry));
        }

        dispatcher ([this, jobGeneration, aliveFlag, built]
        {
            if (! aliveFlag->load())
                return;   // Index wurde zerstört, Ergebnis verwerfen

            if (jobGeneration != generation.load())
                return;   // veralteter Build — der jüngere gewinnt

            entries = std::move (*built);
            ready   = true;

            if (onIndexReady != nullptr)
                onIndexReady();
        });
    });
}

std::vector<juce::String> BrowserSearchIndex::query (const juce::String& needle) const
{
    JUCE_ASSERT_MESSAGE_THREAD

    std::vector<juce::String> hits;

    const auto trimmed = needle.trim().toLowerCase();
    if (trimmed.isEmpty())
        return hits;

    for (const auto& entry : entries)
        if (entry.haystack.contains (trimmed))
            hits.push_back (entry.itemId);

    return hits;
}

} // namespace conduit
