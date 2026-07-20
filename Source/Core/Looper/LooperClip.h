#pragma once

#include <atomic>
#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>

namespace conduit
{

//==============================================================================
/**
    Ein committeter Loop-Clip des Looper-Vollausbaus (M2/M3) — right-sized
    beim Commit allokiert (Message Thread), per Pointer an den Audio-Thread
    gereicht (LooperBank-Command-Queue), nie auf dem Audio-Thread freigegeben
    (Retire-Queue → serviceMessageThread).

    Buffer-Layout (Muster LooperEngine-Voice): [0, crossfadeSamples) =
    Lead-in (Material VOR dem Loop-Start, fürs Wrap-Blending),
    [crossfadeSamples, crossfadeSamples + numContentSamples) = der Content.

    Parameter-Modell (M3, Plan-Entscheidung): STAGED vs. ACTIVE.

      - STAGED [MT schreibt]: die Nutzer-Intention (Rate, Länge, Fenster,
        Reverse + Interpretations-Flags). Jede Stage-Operation schreibt den
        KOMPLETTEN Satz und inkrementiert paramVersion (release) — die
        Flags beschreiben, wie der Satz anzuwenden ist, und bleiben Teil
        des Satzes (kein One-Shot-Race).
      - ACTIVE [nur Audio]: die klingenden Parameter. Der Audio-Thread
        wendet eine neue Version am Blockanfang mit SEINEM exakten
        Playhead positions-kontinuierlich an (geschlossene Formel,
        LooperClipMath) — racefrei, kein MT-Schätzwert. Erzeugt die
        Anwendung einen inhärenten Lese-Sprung (z. B. ÷2 „erste Hälfte"
        aus der zweiten), wird sie hinter einem 5-ms-Splice-Duck
        ausgeführt (Dip statt Klick, Muster Snap-Declick).
      - activeAnchorBeat gehört dem Audio-Thread: Retrigger setzt ihn auf
        den Ausführungs-Beat (Phase 0 am Launch-Punkt), Start lässt ihn
        stehen (phasenstarr, Endlesss-Gefühl).

    exportPins: Save-Geste (M9) hält den Clip am Leben — die Freigabe
    eines Graveyard-Clips wartet, bis der Zähler 0 ist.
*/
struct LooperClip
{
    LooperClip() = default;

    juce::AudioBuffer<float> buffer;

    //==========================================================================
    // Konstant ab Commit (MT schreibt vor der Publikation)
    int    numContentSamples = 0;
    int    crossfadeSamples  = 0;
    double contentBeats = 0.0;
    double samplesPerBeatRecorded = 0.0;
    std::uint64_t commitStartSample = 0;
    double commitEndBeat = 0.0;   // bar-locked Original-Anker („Reset mit Sync")
    std::uint32_t clipId = 0;
    int    commitBars = 0;

    // Beim Commit eingefrorene Quellfarbe (0x00RRGGBB, 0 = keine) — der
    // Editor setzt sie direkt nach dem Commit (Zellfläche/Patch-OUT-Ports,
    // User-Skizze 19.07.2026); MT schreibt/liest, atomic nur zur Sicherheit
    std::atomic<std::uint32_t> sourceRgb { 0 };

    //==========================================================================
    // STAGED [MT → Audio]: Nutzer-Intention, Anwendung am Blockanfang
    std::atomic<double> stagedRate { 1.0 };
    std::atomic<double> stagedLengthBeats { 0.0 };
    std::atomic<double> stagedWindowOffsetBeats { 0.0 };
    std::atomic<bool>   stagedReversed { false };

    // Interpretations-Flags des Staged-Satzes:
    std::atomic<bool> windowFollowsPhase { false };  // ÷2 „aktuelle Hälfte": Fenster aus Apply-Phase
    std::atomic<bool> applyAtWrap { false };         // Reverse-Modus „an der Loop-Grenze"
    std::atomic<bool> resetAnchorToGrid { false };   // „Reset mit Sync": Anker → commitEndBeat

    std::atomic<std::uint32_t> paramVersion { 0 };

    //==========================================================================
    // ACTIVE [nur Audio-Thread]
    double activeAnchorBeat = 0.0;
    double activeRate = 1.0;
    double activeLengthBeats = 0.0;
    double activeWindowOffsetBeats = 0.0;
    bool   activeReversed = false;
    std::uint32_t appliedVersion = 0;

    // Splice-Duck (Audio): Dip vor Anwendungen mit Lese-Sprung
    bool  splicePending = false;
    float spliceGain = 1.0f;
    float spliceStartGain = 1.0f;   // Block-Snapshot fürs Rendering
    float spliceStep = 0.0f;
    std::uint64_t lastSeenBlock = 0;  // Param-Apply genau einmal pro Block

    //==========================================================================
    // Anzeige-Phase 0..1 [Audio schreibt einmal pro Block, UI liest] —
    // Progress-Sweep/Takt-Anzeige (die Active-Felder sind Audio-only)
    std::atomic<float> displayPhase01 { 0.0f };

    // Save-Geste (M9): > 0 = Export liest gerade aus dem Buffer
    std::atomic<int> exportPins { 0 };

    /** RAM-Konto-Beitrag (LooperBank-Budget). */
    [[nodiscard]] std::int64_t allocatedBytes() const noexcept
    {
        return static_cast<std::int64_t> (buffer.getNumChannels())
                 * static_cast<std::int64_t> (buffer.getNumSamples())
                 * static_cast<std::int64_t> (sizeof (float))
             + static_cast<std::int64_t> (sizeof (LooperClip));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperClip)
};

} // namespace conduit
