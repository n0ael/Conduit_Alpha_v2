#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>

#include <juce_core/juce_core.h>

#include "Interfaces/IClockSource.h"
#include "Util/SpscQueue.h"

namespace conduit
{

//==============================================================================
/** Ein empfangener Link-Audio-Buffer als POD-Slot (docs/LinkAudio.md,
    Empfangen Phase 2). Rohe int16-Samples — Int16→Float passiert erst im
    Audio-Thread, der Link-Thread macht nur memcpy. */
struct LinkReceiveSlot
{
    /** ≥ max Netzwerk-Buffer des SDK (512 Samples, LinkAudioRenderer-
        Referenz) mit Reserve — größere Buffer werden verworfen (Zähler). */
    static constexpr int maxSamples = 1024;

    std::int16_t  samples[maxSamples];
    int           numFrames   = 0;
    int           numChannels = 0;
    double        sampleRate  = 48000.0;
    double        tempo       = 120.0;   // Session-Tempo beim Senden
    double        beatBegin   = 0.0;     // lokale Beat-Zeit am Buffer-Anfang
    std::uint64_t count       = 0;       // Sequenznummer des Senders

    /** Buffer-Dauer im Beat-Raum — exakt bei konstantem Tempo; Sprünge
        innerhalb des Latenzfensters fängt der Render-Reset ab. */
    [[nodiscard]] double endBeat() const noexcept
    {
        return (sampleRate > 0.0 && tempo > 0.0)
                 ? beatBegin + (static_cast<double> (numFrames) / sampleRate) * (tempo / 60.0)
                 : beatBegin;
    }
};

static_assert (std::is_trivially_copyable_v<LinkReceiveSlot>,
               "LinkReceiveSlot reist durch die SpscQueue — muss trivially copyable sein");

//==============================================================================
/**
    Beat-alignter Wiedergabe-Strom für Link Audio Receive
    (docs/LinkAudio.md, Empfangen Phase 2 — Muster: SDK-Referenz
    examples/linkaudio/LinkAudioRenderer.hpp).

    Kette: Link-Thread pusht beat-gestempelte Slots → SpscQueue →
    der Audio-Thread zieht sie in einen lokalen Slot-Ring (nur Audio-
    Thread, kein Atomic) und rendert pro Block das Beat-Fenster
    [localBeat − latency, …) mit Re-Pitching (frameIncrement über die
    Slot-Kette, Catmull-Rom-Interpolation) — deckt SampleRate-Differenz
    und Tempoänderungen ab. Nie naives FIFO (v1-Drift-Lektion).

    Underflow / „zu neu" / Beat-Sprung → Reset auf Stille und Re-Init am
    Ziel-Fenster; Übergänge werden über eine 5-ms-Rampe declickt
    (Looper-Duck-Lektion). Die effektive Hörlatenz IST latencyMs.

    Thread-Ownership (strikt):
      - pushBuffer()            → NUR Link-Thread (Producer)
      - renderBlock()           → NUR Audio-Thread (Consumer), allocation-frei
      - requestReset()          → Message Thread (Flag, konsumiert der
                                  Audio-Thread am Blockanfang — Muster 4.5)
      - getBufferedSeconds(), getStatusForUi(), Zähler → beliebiger Thread
      - ctor/dtor               → Message Thread, außer Betrieb
*/
class LinkReceiveStream final
{
public:
    LinkReceiveStream();

    //==========================================================================
    /** UI-Status (atomic, beliebiger Thread). */
    enum class Status : int
    {
        idle      = 0,   // keine Slots — nichts empfangen
        waiting   = 1,   // Slots da, aber Latenzfenster noch nicht gefüllt
        streaming = 2    // rendert
    };

    //==========================================================================
    // Link-Thread (Producer)

    /** Kopiert einen empfangenen Buffer in die Queue. false + Zähler bei
        oversize (> maxSamples), leeren Frames oder voller Queue. beatBegin
        ist die vom LinkClock-Wrapper gestempelte lokale Beat-Zeit — Buffer
        aus fremden Sessions (nullopt) filtert der Aufrufer. */
    bool pushBuffer (const std::int16_t* interleavedSamples,
                     int numFrames, int numChannels,
                     double sampleRate, double tempo,
                     double beatBegin, std::uint64_t count) noexcept;

    //==========================================================================
    // Audio-Thread (Consumer)

    /** Rendert numFrames in left/right (mono-Quelle → beide Kanäle,
        > 2 Kanäle → erste zwei). latencyMs = Monitoring-Latenz (Parameter,
        Dual-State 6.1). Allocation- und lock-frei. */
    void renderBlock (float* left, float* right, int numFrames,
                      const ClockState& clock, float latencyMs) noexcept;

    //==========================================================================
    // Message Thread

    /** Verwirft Bestand + Render-Zustand am nächsten Blockanfang —
        für Source-/Kanal-Wechsel. */
    void requestReset() noexcept { resetRequested.store (true, std::memory_order_release); }

    //==========================================================================
    // Beliebiger Thread (Atomics)

    [[nodiscard]] Status getStatusForUi() const noexcept
    {
        return static_cast<Status> (uiStatus.load (std::memory_order_relaxed));
    }

    /** Gepufferte Sekunden hinter der aktuellen Leseposition (Tuning-Hilfe
        für latencyMs in der UI). */
    [[nodiscard]] float getBufferedSeconds() const noexcept
    {
        return bufferedSeconds.load (std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t getDroppedPushes()  const noexcept { return droppedPushes.load (std::memory_order_relaxed); }
    [[nodiscard]] std::uint32_t getSequenceGaps()   const noexcept { return sequenceGaps.load (std::memory_order_relaxed); }
    [[nodiscard]] std::uint32_t getRenderResets()   const noexcept { return renderResets.load (std::memory_order_relaxed); }

private:
    //==========================================================================
    static constexpr int    kQueueCapacity = 128;  // Link→Audio Transfer (Block-zu-Block)
    static constexpr int    kRingCapacity  = 512;  // Bestand fürs Latenzfenster (Zweierpotenz!)
    static constexpr double kFadeSeconds   = 0.005;

    // ---- nur Audio-Thread ----
    [[nodiscard]] LinkReceiveSlot&       ringAt (int index) noexcept;
    [[nodiscard]] const LinkReceiveSlot& ringAt (int index) const noexcept;

    void drainQueueIntoRing() noexcept;
    void dropFrontSlot() noexcept;
    void resetRenderState (bool countAsReset) noexcept;
    void renderSilence (float* left, float* right, int numFrames, double sampleRate) noexcept;

    /** Sample eines Frames im Ring-Strom (frameIndex relativ zur Ring-Front),
        int16→float; hinter dem Ketten-Ende: 0. cursorSlot/cursorBase
        beschleunigen die monoton steigenden Zugriffe des Render-Loops. */
    [[nodiscard]] float sampleAt (std::int64_t frameIndex, int channel) noexcept;

    SpscQueue<LinkReceiveSlot> queue { kQueueCapacity };

    // Slot-Ring: nur der Audio-Thread liest/schreibt (kein Atomic, Doku oben).
    // Bewusst als Heap-Block über unique_ptr — ~1 MB, gehört nicht auf Stacks.
    std::unique_ptr<std::array<LinkReceiveSlot, kRingCapacity>> ring;
    int ringStart = 0;   // Index (maskiert) des ältesten Slots
    int ringCount = 0;

    std::optional<double> startReadPos;   // Frame-Position in der Slot-Kette
    int    cursorSlot = 0;                // sampleAt-Cursor (Ring-relativ)
    std::int64_t cursorBase = 0;          // erster Frame-Index von cursorSlot

    float  fadeGain       = 0.0f;         // Declick-Rampe 0..1
    float  lastLeft       = 0.0f;         // letzte Ausgabe (Decay bei Abriss)
    float  lastRight      = 0.0f;

    // ---- nur Link-Thread ----
    std::uint64_t lastCount    = 0;
    bool          hasLastCount = false;

    // ---- Thread-übergreifend (Atomics) ----
    std::atomic<bool>          resetRequested { false };
    std::atomic<int>           uiStatus { static_cast<int> (Status::idle) };
    std::atomic<float>         bufferedSeconds { 0.0f };
    std::atomic<std::uint32_t> droppedPushes { 0 };
    std::atomic<std::uint32_t> sequenceGaps { 0 };
    std::atomic<std::uint32_t> renderResets { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinkReceiveStream)
};

} // namespace conduit
