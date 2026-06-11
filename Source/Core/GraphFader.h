#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace conduit
{

//==============================================================================
/**
    Master-Fade für den glitch-freien Graph-Swap (CLAUDE.md 5.2).

    Sitzt hinter dem AudioProcessorGraph im Audio-Pfad und rampt das
    Gesamtsignal vor jedem Topologie-Swap auf Stille (Schritt 2) und nach
    dem Swap wieder hoch (Schritt 4). JUCE rebuildet den Rendering-Plan
    dadurch auf Stille — kein Knacksen.

    Thread-Ownership (Handshake ausschließlich über std::atomic, 5.6):
      - prepare()/reset()                 → Message Thread, Audio gestoppt
      - beginFadeOut()/beginFadeIn()      → Message Thread (GraphManager)
      - isFadeOutComplete()               → Message Thread (Polling via AsyncUpdater)
      - process()                         → Audio Thread, lock-free, allocation-free
*/
class GraphFader final
{
public:
    enum class Phase : int
    {
        active    = 0,
        fadingOut = 1,
        fadingIn  = 2
    };

    GraphFader() = default;

    //==========================================================================
    // Message Thread

    /** Vor Audio-Start aufrufen (EngineProcessor::prepareToPlay). */
    void prepare (double sampleRate);

    /** Audio gestoppt (releaseResources). Ein unprepared Fader kann keine
        Stille liefern — der GraphManager swappt dann ohne Fade. */
    void reset() noexcept;

    [[nodiscard]] bool isPrepared() const noexcept;

    /** Rampzeit, default 5ms (CLAUDE.md 5.6). Wirkt ab dem nächsten prepare().
        Per-Node-Konfiguration folgt mit den Node-Lifecycles. */
    void setRampSeconds (double seconds) noexcept;

    /** Schritt 2: Fade-Out anstoßen — die Graph-Topologie bleibt unverändert.
        Setzt fadeComplete zurück. */
    void beginFadeOut() noexcept;

    /** Schritt 4: Fade-In anstoßen (nach dem Topologie-Swap). */
    void beginFadeIn() noexcept;

    /** true, sobald der Audio Thread Stille erreicht hat (Schritt 2 → 3).
        Ab dann ist jeder weitere Block garantiert still. */
    [[nodiscard]] bool isFadeOutComplete() const noexcept;

    [[nodiscard]] Phase getCurrentPhase() const noexcept;

    //==========================================================================
    // Audio Thread

    /** Lock-free, allocation-free. Nach graph.processBlock() aufrufen. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

private:
    // Nur Audio Thread (bzw. prepare() bei gestopptem Audio)
    juce::SmoothedValue<float> gain { 1.0f };
    Phase lastAppliedPhase = Phase::active;

    // Cross-Thread-Handshake
    std::atomic<Phase> requestedPhase { Phase::active };
    std::atomic<bool> fadeComplete { false };
    std::atomic<bool> prepared { false };

    double rampSeconds = 0.005;  // 5ms default (5.6)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphFader)
};

} // namespace conduit
