#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>

#include "BarSampleAnchors.h"
#include "Core/Capture/CaptureService.h"
#include "Interfaces/IClockSource.h"
#include "LooperMath.h"

namespace conduit
{

//==============================================================================
/**
    Loop-Playback des Retro-Loopers (Looper-Baustein B5) — Engine-Level wie
    das Metronom (User-Entscheidung 07/2026): läuft im EngineProcessor NACH
    Master-Tap/Waveform-Binner und VOR dem Metronom, addiert auf ein
    wählbares Stereo-Anker-Paar. Bewusst OHNE EngineProcessor-Abhängigkeit
    (nur ClockState + CaptureService + AudioBuffer) — das spätere patchbare
    LooperModule hostet dieselbe Klasse, nur der Mix-Punkt wandert.

    Commit [Message Thread]: die letzten N KOMPLETTEN Takte, sample-exakt
    über die BarSampleAnchors adressiert, werden über das Export-Halte-
    Protokoll des CaptureChannel (tryBeginExportRead — zählerbasiert,
    parallel zum CaptureWriter legal) in den inaktiven Voice-Buffer
    kopiert. Bis zu ~23 MB memcpy gehören nie in den Audio-Callback.
    Unlesbare Teilbereiche (Gate war zu) werden mit Stille gefüllt.
    Zusätzlich liest der Commit crossfadeSamples VOR dem Loop-Start als
    Lead-in — das Material fürs Wrap-Blending (unten).

    Phase: BEAT-ABGELEITET, kein freilaufender Zähler. Der Loop deckt die
    Session-Beats [B−L, B) (B = Commit-Taktgrenze): pro Sample gilt
    readPos = wrap(sessionBeat − B, L) · samplesPerBeatRecorded, linear
    interpoliert. Kein Drift möglich, Peer-Join-Sprünge folgen automatisch;
    Session-Tempo ≠ Aufnahme-Tempo ⇒ Varispeed (dokumentierte MVP-Grenze).
    Der Start ist deshalb SOFORT und phasenstarr — es gibt keine freie
    Phase, die auf eine Taktgrenze warten müsste (Endlesss-Gefühl statt
    Launch-Wartezeit).

    Playhead statt roher ClockState-Beat (3.1, v1-Lektion): der
    beatAtBlockStart des Blocks stammt aus der Link-Wall-Clock beim
    Callback-Eintritt und jittert um den Scheduling-Versatz (±0.5–2 ms —
    als direkte Lese-Basis springt der Lesekopf an jeder Blockgrenze um
    Dutzende Samples: hörbare Körnung). Die Engine führt deshalb einen
    sample-kontinuierlichen Beat-Playhead: nomineller Fortschritt
    beatsPerSample, pro Block slew-limitiert (maxSlewRatio, unhörbares
    Varispeed) auf die MESSUNG korrigiert. Die Messung ist jitter-frei aus
    der SampleClock abgeleitet: Beat des jüngsten Takt-Ankers +
    (blockStartSample − Anker-Sample)·beatsPerSample — dieselben Anker,
    die den Loop geschnitten haben, die Phase ist damit per Konstruktion
    deckungsgleich mit dem Schnitt. Sprünge > snapThresholdBeats
    (Peer-Join, Transport-Remap) snappen sofort.

    Wrap-Crossfade: bar-geschnittenes Material aus kontinuierlichem Spiel
    ist am Wrap diskontinuierlich. Die letzten crossfadeSamples der Loop-
    Periode blenden equal-power vom Loop-Ende auf den Lead-in (das Material
    unmittelbar VOR dem Loop-Start) — am Wrap landet die Blende exakt auf
    dem Loop-Start-Sample: stetig, ohne Knacks.

    Voices: 2 × Stereo × maxLoopSeconds, in prepare() allokiert (~46 MB
    @48 kHz — dokumentierter RAM-Preis, Commits > maxLoopSeconds schlagen
    fehl). Zustandsmaschine pro Voice:

        free → filling [MT] → pendingActivate → active → fadingOut → free

    Re-Commit ist damit glitch-frei: die neue Voice faded 5 ms ein, die
    alte 5 ms aus (beide phasenstarr). Stop faded sofort aus. Bewusst kein
    isPlaying-Gate — Conduit läuft frei (Muster Metronom).

    Threading: prepare()/commit()/stop()/setAnchor() → Message Thread;
    process() → Audio Thread. Übergabe über die Voice-States (release/
    acquire) — die Parameter einer Voice schreibt der MT ausschließlich im
    Zustand filling, der Audio Thread liest sie erst nach pendingActivate.
*/
class LooperEngine
{
public:
    static constexpr double maxLoopSeconds = 60.0;
    static constexpr double crossfadeSeconds = 0.005;
    static constexpr int numVoices = 2;

    /** Maximale Playhead-Korrektur als Anteil des nominellen Beat-
        Fortschritts (0.002 = 0.2 % Varispeed ≈ 3.5 Cent — FM-Seitenbänder
        bei Blockrate weit unter −40 dB, deckt Audio-/Link-Clock-Drift
        (≤ ~200 ppm) mit 10× Reserve). */
    static constexpr double maxSlewRatio = 0.002;

    /** Mess-Sprünge oberhalb dieser Schwelle snappen den Playhead sofort
        (Peer-Join/Transport-Remap) statt minutenlang zu schleifen. */
    static constexpr double snapThresholdBeats = 0.15;

    LooperEngine() = default;

    /** [Message Thread, Audio steht — prepareToPlay] allokiert die
        Voice-Buffer für maxLoopSeconds und verwirft laufende Loops
        (SampleRate-Wechsel invalidiert Positionen und Inhalte). */
    void prepare (double sampleRate);

    /** [Message Thread] Die letzten `bars` kompletten Takte committen und
        sofort phasenstarr abspielen. leftIndex/rightIndex = Capture-
        Indizes der Looper-Quelle (Mono: beide gleich). Fehlerfälle:
        keine Quelle, noch keine `bars` kompletten Takte, Anker älter als
        das Anker-Fenster, Loop länger als maxLoopSeconds, kein freier
        Voice-Slot. */
    [[nodiscard]] juce::Result commit (int bars, const CaptureService& capture,
                                       int leftIndex, int rightIndex,
                                       const BarSampleAnchors& anchors);

    /** [Message Thread] Playback mit 5-ms-Fade beenden. */
    void stop() noexcept;

    /** Stereo-Anker: Loop auf Kanäle pairIndex*2 / +1 (Muster Metronome). */
    void setAnchor (int pairIndex) noexcept
    {
        anchor.store (juce::jmax (0, pairIndex), std::memory_order_relaxed);
    }

    /** [Audio Thread] Loop auf die Anker-Kanäle addieren.
        blockStartSample = SampleClock-Position des Block-Anfangs
        (Kontrakt CaptureService.h), anchors = dieselbe Instanz, die den
        Commit geschnitten hat — Basis der jitter-freien Beat-Messung. */
    void process (juce::AudioBuffer<float>& buffer, int numOutputChannels,
                  const ClockState& clock, std::uint64_t blockStartSample,
                  const BarSampleAnchors& anchors) noexcept;

    //==========================================================================
    // Status [beliebiger Thread]

    [[nodiscard]] bool isPlaying() const noexcept;

    /** Takte des zuletzt committeten Loops (0 = keiner). */
    [[nodiscard]] int getLoopBars() const noexcept
    {
        return isPlaying() ? committedBars.load (std::memory_order_relaxed) : 0;
    }

private:
    enum class VoiceState : int { free = 0, filling, pendingActivate, active, fadingOut };

    struct Voice
    {
        // Buffer-Layout: [0, crossfade) = Lead-in (Material VOR dem
        // Loop-Start), [crossfade, crossfade + numLoopSamples) = der Loop
        juce::AudioBuffer<float> buffer;

        std::atomic<VoiceState> state { VoiceState::free };

        // [MT schreibt im Zustand filling; Audio liest nach pendingActivate]
        double loopEndBeat = 0.0;
        double lengthBeats = 0.0;
        double samplesPerBeatRecorded = 0.0;
        int    numLoopSamples = 0;

        // Nur Audio Thread
        float gain = 0.0f;
    };

    /** Kanal chunked in den Voice-Buffer lesen — unlesbare Bereiche
        bleiben Stille. [MT, Export-Halte-Protokoll] */
    static void readChannelChunked (const CaptureChannel* channel,
                                    std::uint64_t startPosition,
                                    float* dest, int numSamples);

    /** Sample einer Voice an (fraktionaler) Loop-Position — linear
        interpoliert, mit Wrap-Crossfade auf den Lead-in. [Audio] */
    [[nodiscard]] float renderVoiceSample (const Voice& voice, int channel,
                                           double loopPosition) const noexcept;

    std::array<Voice, static_cast<std::size_t> (numVoices)> voices;

    // Nur Audio Thread: sample-kontinuierlicher Beat-Playhead (Klassendoku)
    double playheadBeat  = 0.0;
    bool   playheadValid = false;

    // Message → Audio
    std::atomic<int>  anchor { 0 };
    std::atomic<bool> stopRequested { false };
    std::atomic<int>  committedBars { 0 };

    // Nur Message Thread (prepare)
    double preparedSampleRate = 0.0;
    int crossfadeSamples = 0;
    int maxLoopSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperEngine)
};

} // namespace conduit
