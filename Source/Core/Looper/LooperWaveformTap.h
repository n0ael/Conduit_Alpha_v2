#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "Core/Capture/CaptureService.h"
#include "Interfaces/IClockSource.h"
#include "LooperMath.h"
#include "Util/SpscQueue.h"

namespace conduit
{

//==============================================================================
/**
    Beat-indizierter Min/Max-Binner der Looper-Quelle (Looper-Baustein B4) —
    der Datenpfad hinter der gestauchten Endlesss-Wellenform.

    Läuft am Block-ENDE auf dem Audio Thread (nach dem Master-Tap-Write:
    Hardware-Kanäle schreibt der Input-Tap am Blockanfang, Modul-Taps der
    Graph, master_l/_r der EngineProcessor — ALLE Quelltypen sind dann für
    diesen Block vollständig im Ring). Der Tap liest die Samples der
    gewählten Quelle über CaptureService::getAudioChannelView() — der Audio
    Thread ist selbst der Ring-Writer, das Lesen im selben Callback ist
    trivial racefrei (kein Export-Protokoll nötig).

    Bins liegen an festen Beat-Rastergrenzen (binsPerBeat = 32, also
    1/32 Beat = 1/128-Note): Bin b deckt Beats [b, b+1)/binsPerBeat. Ein Bin
    wird abgeschlossen, sobald sein End-Beat hinter dem Block-Ende liegt,
    und als {index, min, max} in die SPSC-Queue gepusht (Muster ScopeModule,
    Konsumentenrolle exklusiv beim LooperWaveformStrip). Beat-indizierte
    Bins machen Tempo-Wechsel trivial (alte Bins bleiben gültig, die
    Segment-Kompression ist reine Beat-Arithmetik) und die UI braucht keine
    Anker-Interpolation.

    Gate-Löcher: liefert der Ring einen Bereich nicht (Gate war zu, Kanal
    idle, keine Quelle), wird ein NULL-Bin gepusht — semantisch korrekt,
    Gate zu heißt Signal unter der Schwelle. Die Looper-Quelle ist normal
    gearmt (B1) und lückenlos.

    Backfill: nach Quellwechsel/Reset sollen die letzten 8 Takte sofort
    sichtbar sein — pro Block werden rückwärts höchstens
    backfillBudgetSamples aus dem Ring nachgebinnt (~65 µs memcpy+scan
    @48k/Block, volle 1024 Bins in ~35 ms). Beat→Sample rückwärts ist eine
    lineare Extrapolation vom aktuellen Block — über historische
    Tempo-Wechsel eine dokumentierte Anzeige-Näherung (der Commit in B5
    bleibt über die BarSampleAnchors exakt).

    Spektrum-View (S1): ZWEITER Ausgabepfad im selben process() — gleiche
    Quelle, gleiche Reset-/Backfill-Logik, eigener Raster-Cursor + eigene
    SPSC-Queue. Pro Spektral-Spalte (1/16 Beat, looper::spectrumColumnsPer-
    Beat) werden die LETZTEN spectrumFftSize Samples bis zum Spalten-Ende
    gelesen (L/R gemittelt, Mono), Hann-gefenstert und via juce::dsp::FFT
    (Ordnung 11, Engine im Konstruktor gebaut — perform ist allocation-
    free) auf looper::spectrumBands log-verteilte Bänder reduziert
    (Band-Wert = Maximum der Magnitude-Bins, dB-normalisiert 0..1,
    looper::spectrumLevel). Kosten ~32 FFTs/s @120 BPM — deshalb bewusst
    ALWAYS-ON: der View-Wechsel in der UI ist instant mit voller Historie.
    Spektral-Backfill hat ein EIGENES Sample-Budget (jede Spalte liest ein
    volles Fenster; volle 512 Spalten in < 1 s nach Quellwechsel).

    Threading: process() NUR Audio Thread; setSource()/prepare() Message
    Thread (Atomics bzw. Audio steht); pop()/popSpectrum() exklusiv beim
    UI-Konsumenten.
*/
class LooperWaveformTap
{
public:
    static constexpr int binsPerBeat = 32;
    static constexpr int historyBins = looper::maxBars
                                       * static_cast<int> (looper::quantumBeats) * binsPerBeat;  // 1024
    static constexpr int spectrumHistoryColumns
        = looper::maxBars * static_cast<int> (looper::quantumBeats)
          * looper::spectrumColumnsPerBeat;  // 512

    struct Bin
    {
        std::int64_t index = 0;   // Beat-Raster: Bin deckt [index, index+1)/binsPerBeat
        float minValue = 0.0f;
        float maxValue = 0.0f;
    };

    struct SpectralColumn
    {
        std::int64_t index = 0;   // Spalten-Raster: [index, index+1)/spectrumColumnsPerBeat
        std::array<float, static_cast<std::size_t> (looper::spectrumBands)> bands {};  // 0..1
    };

    LooperWaveformTap();

    /** [Message Thread, Audio steht — prepareToPlay] verwirft Bins und
        Spektral-Spalten, berechnet die Band-Grenzen für die sampleRate
        neu; der nächste process()-Block setzt neu auf (inkl. Backfill). */
    void prepare (double sampleRate) noexcept
    {
        bands.compute (sampleRate);
        sourceVersion.fetch_add (1, std::memory_order_release);

        // Konsument steht (Audio steht, UI folgt)
        Bin discardedBin;
        while (queue.pop (discardedBin)) {}
        SpectralColumn discardedColumn;
        while (spectrumQueue.pop (discardedColumn)) {}
    }

    /** [Message Thread] Capture-Indizes der Quelle (−1 = keine; Mono =
        beide gleich). Jeder Wechsel triggert Reset + Backfill im Audio
        Thread. */
    void setSource (int leftIndex, int rightIndex) noexcept
    {
        sourceLeft.store (leftIndex, std::memory_order_relaxed);
        sourceRight.store (rightIndex, std::memory_order_relaxed);
        sourceVersion.fetch_add (1, std::memory_order_release);
    }

    /** [Audio Thread, am Block-ENDE nach dem Master-Tap-Write]
        blockStartSample = SampleClock-Stand des ersten Samples dieses
        Blocks (now() − numSamples). Allocation-free. */
    void process (const ClockState& clock, const CaptureService& capture,
                  std::uint64_t blockStartSample, int numSamples) noexcept;

    /** [UI-Thread, VBlank — Konsumentenrolle exklusiv] */
    bool pop (Bin& bin) noexcept { return queue.pop (bin); }

    /** [UI-Thread, VBlank — Konsumentenrolle exklusiv] Spektral-Spalten. */
    bool popSpectrum (SpectralColumn& column) noexcept { return spectrumQueue.pop (column); }

private:
    /** Bin lesen (beide Quellseiten, min/max zusammengefasst) und pushen.
        Liefert die gelesene Sample-Zahl (Backfill-Budget). */
    int emitBin (std::int64_t binIndex, const CaptureService& capture,
                 double beatStart, double beatsPerSample,
                 std::uint64_t blockStartSample) noexcept;

    /** Spektral-Spalte: Fenster lesen (Mono-Mix), FFT, Bänder pushen.
        Liefert die budgetierte Sample-Zahl (0 vor Signalbeginn). */
    int emitSpectrumColumn (std::int64_t columnIndex, const CaptureService& capture,
                            double beatStart, double beatsPerSample,
                            std::uint64_t blockStartSample) noexcept;

    static constexpr int backfillBudgetSamples = 16384;
    static constexpr int spectrumBackfillBudgetSamples = 32768;  // 8–16 Spalten/Block
    static constexpr int scratchSamples = 8192;  // 1/32 Beat bis ~11 BPM

    SpscQueue<Bin> queue { 8192 };
    SpscQueue<SpectralColumn> spectrumQueue { 1024 };
    std::array<float, static_cast<std::size_t> (scratchSamples)> scratch {};

    // FFT-Zubehör (Konstruktion/compute nur MT bei stehendem Audio;
    // perform/Lesen nur Audio Thread — allocation-free, Klassendoku)
    juce::dsp::FFT fft { looper::spectrumFftOrder };
    std::array<float, static_cast<std::size_t> (looper::spectrumFftSize)> window {};
    std::array<float, static_cast<std::size_t> (2 * looper::spectrumFftSize)> fftData {};
    std::array<float, static_cast<std::size_t> (looper::spectrumFftSize)> spectrumScratch {};
    looper::SpectrumBands bands;

    // Message → Audio
    std::atomic<int> sourceLeft  { -1 };
    std::atomic<int> sourceRight { -1 };
    std::atomic<std::uint32_t> sourceVersion { 1 };

    // Nur Audio Thread
    std::uint32_t seenVersion = 0;   // != sourceVersion → Reset + Backfill
    std::int64_t nextBin = 0;        // nächster live abzuschließender Bin
    std::int64_t backfillBin = -1;   // rückwärts laufender Backfill-Cursor
    std::int64_t backfillOldest = 0; // ältester Bin des Backfill-Fensters
    std::int64_t nextColumn = 0;             // Spektral-Pendants zu den Bin-Cursorn
    std::int64_t backfillColumn = -1;
    std::int64_t backfillColumnOldest = 0;
    double previousBeatEnd = 0.0;
    bool beatValid = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperWaveformTap)
};

} // namespace conduit
