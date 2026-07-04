#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include <juce_events/juce_events.h>

namespace conduit
{

//==============================================================================
/**
    Suchindex des Browser-Panels: exakte, case-insensitive Substring-Suche
    über Name, Kategorie und Tags (~63 Module heute, skaliert locker auf
    400+ — mehr Raffinesse braucht es nicht).

    Der AUFBAU läuft im Hintergrund (geteilter 1-Thread-Pool des Editors):
    rebuildAsync() kopiert die Quellen in einen Pool-Job, der die
    lowercased Haystacks baut und das Ergebnis via MessageManager::
    callAsync zurück auf den Message Thread reicht — ein Generation-
    Zähler verwirft veraltete Builds, ein Alive-Flag schützt gegen
    Destruktion während des Jobs. query() liest NUR auf dem Message
    Thread über den zuletzt publizierten Index. Der Audio-Thread ist nie
    beteiligt (CLAUDE.md 3.1).
*/
class BrowserSearchIndex final
{
public:
    struct Source
    {
        juce::String itemId;    // factoryKey (M6: auch Datei-Ids)
        juce::String name;
        juce::String category;
        juce::String tags;      // Leerzeichen-getrennt, lowercase
    };

    /** Test-Seam: bringt das Build-Ergebnis auf den Message Thread.
        Default (leer) = MessageManager::callAsync — Tests injizieren
        eine Queue und pumpen selbst (runDispatchLoopUntil existiert mit
        JUCE_MODAL_LOOPS_PERMITTED=0 nicht). Wird vom POOL-Thread
        aufgerufen, muss also thread-safe sein. */
    using Dispatcher = std::function<void (std::function<void()>)>;

    explicit BrowserSearchIndex (juce::ThreadPool& workerToUse,
                                 Dispatcher dispatcherToUse = {});
    ~BrowserSearchIndex();

    /** [Message Thread] Baut den Index im Pool neu auf; das jüngste
        rebuildAsync gewinnt (Generation-Zähler). */
    void rebuildAsync (std::vector<Source> sources);

    /** [Message Thread] itemIds aller Einträge, deren Haystack die
        Nadel (case-insensitiv) enthält; leer bei leerer Nadel. */
    [[nodiscard]] std::vector<juce::String> query (const juce::String& needle) const;

    [[nodiscard]] bool isReady() const noexcept { return ready; }

    /** [Message Thread] Nach jedem publizierten Build. */
    std::function<void()> onIndexReady;

private:
    struct Entry
    {
        juce::String itemId;
        juce::String haystack;   // lowercase: name + category + tags + id
    };

    juce::ThreadPool& worker;
    Dispatcher dispatcher;                  // nach Konstruktion unveränderlich

    std::vector<Entry> entries;             // nur Message Thread
    bool ready = false;

    std::atomic<int> generation { 0 };
    std::shared_ptr<std::atomic<bool>> alive
        = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserSearchIndex)
};

} // namespace conduit
