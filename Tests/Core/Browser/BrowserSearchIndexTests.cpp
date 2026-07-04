#include <catch2/catch_test_macros.hpp>

#include "Core/Browser/BrowserSearchIndex.h"
#include "TestDispatcher.h"

namespace
{
struct IndexRig
{
    /** Pumpt die Dispatcher-Queue, bis der Build publiziert ist. */
    bool waitForReady()
    {
        return dispatcher.pumpUntil ([this] { return index.isReady(); });
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    juce::ThreadPool worker { juce::ThreadPoolOptions{}.withNumberOfThreads (1) };
    conduit::test::QueueDispatcher dispatcher;
    conduit::BrowserSearchIndex index { worker, dispatcher.fn() };
};

std::vector<conduit::BrowserSearchIndex::Source> demoSources()
{
    return {
        { "airwindows_chamber", "Chamber", "Reverb/Delay", "reverb chamber space hall" },
        { "airwindows_tube2",   "Tube2",   "Distortion/Saturation", "tube valve warmth" },
        { "lfo",                "LFO",     "LFO", "lfo oscillator modulation" },
        { "seq",                juce::String::fromUTF8 ("Sequenzer Über"), "Sequencer",
          juce::String::fromUTF8 ("überblende pattern") },
    };
}
} // namespace

//==============================================================================
TEST_CASE ("Suchindex: Substring über Name/Kategorie/Tags, case-insensitiv",
           "[browser][search]")
{
    IndexRig rig;
    rig.index.rebuildAsync (demoSources());
    REQUIRE (rig.waitForReady());

    // Name (Groß-/Kleinschreibung egal)
    REQUIRE (rig.index.query ("CHAM") == std::vector<juce::String> { "airwindows_chamber" });

    // Kategorie
    REQUIRE (rig.index.query ("reverb/") == std::vector<juce::String> { "airwindows_chamber" });

    // Tags
    REQUIRE (rig.index.query ("valve") == std::vector<juce::String> { "airwindows_tube2" });

    // Mehrere Treffer (Substring "l" wäre zu breit — "lfo" trifft Name+Tags)
    REQUIRE (rig.index.query ("lfo") == std::vector<juce::String> { "lfo" });

    // Umlaute
    REQUIRE (rig.index.query (juce::String::fromUTF8 ("über"))
                 == std::vector<juce::String> { "seq" });

    // Leere/Whitespace-Nadel liefert nichts (Navigation bleibt)
    REQUIRE (rig.index.query ("").empty());
    REQUIRE (rig.index.query ("   ").empty());

    // Kein Treffer
    REQUIRE (rig.index.query ("xyzzy").empty());
}

TEST_CASE ("Suchindex: der jüngste Build gewinnt (Generation-Zähler)",
           "[browser][search]")
{
    IndexRig rig;

    int readyCount = 0;
    rig.index.onIndexReady = [&readyCount] { ++readyCount; };

    // Zwei Rebuilds direkt hintereinander — nur der zweite Datensatz
    // darf am Ende sichtbar sein, egal welcher Job zuerst fertig wird
    rig.index.rebuildAsync ({ { "old", "Veraltet", "Alt", "alt" } });
    rig.index.rebuildAsync ({ { "new", "Aktuell", "Neu", "neu" } });

    REQUIRE (rig.waitForReady());

    // Restliche Ergebnisse (verworfener alter Build) abarbeiten
    rig.dispatcher.pumpUntil ([] { return false; }, 100);

    REQUIRE (rig.index.query ("veraltet").empty());
    REQUIRE (rig.index.query ("aktuell") == std::vector<juce::String> { "new" });
    REQUIRE (readyCount == 1);   // der alte Build publiziert nie
}
