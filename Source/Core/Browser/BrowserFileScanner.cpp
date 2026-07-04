#include "BrowserFileScanner.h"

namespace conduit
{

namespace
{
    juce::String summarizeFormat (const juce::AudioFormatReader& reader)
    {
        juce::String summary;
        summary << juce::String (reader.sampleRate / 1000.0, 1)
                       .trimCharactersAtEnd ("0").trimCharactersAtEnd (".") << "k";

        if (reader.bitsPerSample > 0)
            summary << " / " << (int) reader.bitsPerSample << " Bit";

        summary << " / " << (reader.numChannels >= 2 ? "st" : "mo");
        return summary;
    }
} // namespace

//==============================================================================
BrowserFileScanner::BrowserFileScanner (juce::ThreadPool& workerToUse,
                                        BrowserSearchIndex::Dispatcher dispatcherToUse)
    : worker (workerToUse),
      dispatcher (dispatcherToUse != nullptr
                      ? std::move (dispatcherToUse)
                      : BrowserSearchIndex::Dispatcher ([] (std::function<void()> fn)
                        { juce::MessageManager::callAsync (std::move (fn)); }))
{
    resources->formatManager.registerBasicFormats();
}

BrowserFileScanner::~BrowserFileScanner()
{
    alive->store (false);
}

void BrowserFileScanner::scanAsync (const juce::String& scanId,
                                    const juce::File& directory,
                                    const juce::String& wildcard,
                                    bool readAudioMetadata)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto jobGeneration = ++generations[scanId];

    // Der Pool-Job darf `this` NIE dereferenzieren (Scanner kann vor dem
    // Pool sterben) — er arbeitet auf dem shared Resources-Block und einer
    // Dispatcher-KOPIE; `this` wandert nur als Wert ins innere Lambda und
    // wird erst nach dem Alive-Check benutzt (beides Message Thread).
    worker.addJob ([this, scanId, directory, wildcard, readAudioMetadata,
                    jobGeneration, aliveFlag = alive, dispatch = dispatcher,
                    shared = resources]
    {
        // [Pool-Thread] — Verzeichnis listen + Header-Metadaten lesen
        auto entries = std::make_shared<std::vector<Entry>>();

        for (const auto& file : directory.findChildFiles (
                 juce::File::findFiles, false, wildcard))
        {
            Entry entry;
            entry.file      = file;
            entry.name      = file.getFileNameWithoutExtension();
            entry.modTimeMs = file.getLastModificationTime().toMilliseconds();

            if (readAudioMetadata)
            {
                const auto key = file.getFullPathName();

                {
                    const juce::ScopedLock lock (shared->cacheLock);
                    if (const auto it = shared->cache.find (key);
                        it != shared->cache.end()
                        && it->second.modTimeMs == entry.modTimeMs)
                    {
                        entries->push_back (it->second.entry);
                        continue;   // unverändert — Reader übersprungen
                    }
                }

                shared->metadataReads.fetch_add (1);

                // Header-only: Reader öffnen liest keine Sample-Daten
                if (const std::unique_ptr<juce::AudioFormatReader> reader {
                        shared->formatManager.createReaderFor (file) })
                {
                    if (reader->sampleRate > 0.0)
                        entry.durationSeconds = (double) reader->lengthInSamples
                                                / reader->sampleRate;
                    entry.formatSummary = summarizeFormat (*reader);
                }

                const juce::ScopedLock lock (shared->cacheLock);
                shared->cache[key] = { entry.modTimeMs, entry };
            }

            entries->push_back (std::move (entry));
        }

        std::sort (entries->begin(), entries->end(),
                   [] (const Entry& a, const Entry& b)
                   { return a.name.compareIgnoreCase (b.name) < 0; });

        dispatch ([this, scanId, jobGeneration, aliveFlag, entries]
        {
            if (! aliveFlag->load())
                return;

            if (const auto it = generations.find (scanId);
                it == generations.end() || it->second != jobGeneration)
                return;   // veralteter Scan — der jüngere gewinnt

            if (onScanComplete != nullptr)
                onScanComplete (scanId, std::move (*entries));
        });
    });
}

} // namespace conduit
