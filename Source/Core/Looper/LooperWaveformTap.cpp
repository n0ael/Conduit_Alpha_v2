#include "LooperWaveformTap.h"

#include <cmath>

namespace conduit
{

LooperWaveformTap::LooperWaveformTap()
{
    // Hann-Fenster einmalig vorberechnen [MT]
    for (int i = 0; i < looper::spectrumFftSize; ++i)
        window[static_cast<std::size_t> (i)] = 0.5f
            * (1.0f - std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (i)
                                / static_cast<float> (looper::spectrumFftSize - 1)));

    // Default-Bänder (48 k) — Tests/Betrieb vor dem ersten prepare();
    // prepare() rechnet für die echte sampleRate nach
    bands.compute (48000.0);

    // FFT-Warmup: eine Transformation auf Stille zwingt jede Lazy-
    // Initialisierung der Engine hierher [MT] — perform im Audio Thread
    // bleibt garantiert allocation-free (RT-Audit-Test sichert das ab)
    fftData.fill (0.0f);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
}

void LooperWaveformTap::process (const ClockState& clock, const CaptureService& capture,
                                 std::uint64_t blockStartSample, int numSamples) noexcept
{
    const auto beatsPerSample = clock.beatsPerSample();
    if (numSamples <= 0 || beatsPerSample <= 0.0)
        return;

    const auto beatStart = clock.beatAtBlockStart;
    const auto beatEnd   = beatStart + beatsPerSample * numSamples;

    // Quellwechsel (Version) oder Beat-Sprung (Peer-Join, erster Block):
    // neu aufsetzen und die letzten 8 Takte zum Backfill vormerken
    const auto version = sourceVersion.load (std::memory_order_acquire);
    if (version != seenVersion || ! beatValid
        || std::abs (beatStart - previousBeatEnd) > 1.0)
    {
        seenVersion = version;
        nextBin = static_cast<std::int64_t> (
                      std::floor (beatStart * static_cast<double> (binsPerBeat)));
        backfillBin    = nextBin - 1;
        backfillOldest = juce::jmax (std::int64_t { 0 },
                                     nextBin - static_cast<std::int64_t> (historyBins));

        nextColumn = static_cast<std::int64_t> (
            std::floor (beatStart * static_cast<double> (looper::spectrumColumnsPerBeat)));
        backfillColumn       = nextColumn - 1;
        backfillColumnOldest = juce::jmax (std::int64_t { 0 },
                                           nextColumn - static_cast<std::int64_t> (spectrumHistoryColumns));
    }

    beatValid = true;
    previousBeatEnd = beatEnd;

    // 1) Live-Bins: alle Raster-Bins abschließen, deren Ende in diesem
    //    Block liegt (der Ring trägt den Block bereits vollständig)
    while (static_cast<double> (nextBin + 1) / static_cast<double> (binsPerBeat) <= beatEnd)
    {
        emitBin (nextBin, capture, beatStart, beatsPerSample, blockStartSample);
        ++nextBin;
    }

    // 2) Backfill rückwärts, budgetiert in SAMPLES — verteilt die 8-Takt-
    //    Historie eines Quellwechsels über wenige Blöcke (Klassendoku)
    auto budget = backfillBudgetSamples;
    while (backfillBin >= backfillOldest && budget > 0)
    {
        budget -= emitBin (backfillBin, capture, beatStart, beatsPerSample, blockStartSample);
        --backfillBin;
    }

    // 3) Spektral-Spalten (S1): gleiche Struktur wie 1)+2), eigener Cursor,
    //    eigenes Budget (jede Spalte liest ein volles FFT-Fenster)
    while (static_cast<double> (nextColumn + 1)
               / static_cast<double> (looper::spectrumColumnsPerBeat) <= beatEnd)
    {
        emitSpectrumColumn (nextColumn, capture, beatStart, beatsPerSample, blockStartSample);
        ++nextColumn;
    }

    auto spectrumBudget = spectrumBackfillBudgetSamples;
    while (backfillColumn >= backfillColumnOldest && spectrumBudget > 0)
    {
        spectrumBudget -= emitSpectrumColumn (backfillColumn, capture,
                                              beatStart, beatsPerSample, blockStartSample);
        --backfillColumn;
    }
}

int LooperWaveformTap::emitBin (std::int64_t binIndex, const CaptureService& capture,
                                double beatStart, double beatsPerSample,
                                std::uint64_t blockStartSample) noexcept
{
    // Beat → absolute SampleClock-Position, linear vom aktuellen Block aus
    // (rückwärts extrapoliert für Backfill-Bins — Anzeige-Näherung)
    const auto sampleForBeat = [&] (double beat) noexcept -> std::int64_t
    {
        const auto offset = (beat - beatStart) / beatsPerSample;
        return static_cast<std::int64_t> (blockStartSample) + std::llround (offset);
    };

    const auto binStartBeat = static_cast<double> (binIndex) / static_cast<double> (binsPerBeat);
    const auto binEndBeat   = static_cast<double> (binIndex + 1) / static_cast<double> (binsPerBeat);

    const auto startSample = sampleForBeat (binStartBeat);
    const auto endSample   = sampleForBeat (binEndBeat);
    const auto binSamples  = static_cast<int> (juce::jlimit (
        std::int64_t { 0 }, std::int64_t { scratchSamples }, endSample - startSample));

    Bin bin;
    bin.index = binIndex;

    bool anyData = false;
    float minValue = 0.0f, maxValue = 0.0f;

    if (startSample >= 0 && binSamples > 0)
    {
        const int indices[] = { sourceLeft.load (std::memory_order_relaxed),
                                sourceRight.load (std::memory_order_relaxed) };

        for (int side = 0; side < 2; ++side)
        {
            // Mono-Quelle (beide Indizes gleich): eine Seite reicht
            if (side == 1 && indices[1] == indices[0])
                break;

            const auto* channel = capture.getAudioChannelView (indices[side]);
            if (channel == nullptr
                || ! channel->read (static_cast<std::uint64_t> (startSample),
                                    scratch.data(), binSamples))
                continue;  // Loch/Gate zu/keine Quelle → Seite trägt nichts bei

            auto range = juce::FloatVectorOperations::findMinAndMax (scratch.data(), binSamples);
            minValue = anyData ? juce::jmin (minValue, range.getStart()) : range.getStart();
            maxValue = anyData ? juce::jmax (maxValue, range.getEnd())   : range.getEnd();
            anyData = true;
        }
    }

    if (anyData)
    {
        bin.minValue = minValue;
        bin.maxValue = maxValue;
    }
    // sonst: Null-Bin (Loch = Stille, Klassendoku)

    queue.push (bin);  // voll → Bin verfällt (UI holt 60×/s, praktisch nie)
    return binSamples;
}

int LooperWaveformTap::emitSpectrumColumn (std::int64_t columnIndex, const CaptureService& capture,
                                           double beatStart, double beatsPerSample,
                                           std::uint64_t blockStartSample) noexcept
{
    // Beat → absolute SampleClock-Position — identische Extrapolation wie
    // emitBin (Backfill = dokumentierte Anzeige-Näherung)
    const auto sampleForBeat = [&] (double beat) noexcept -> std::int64_t
    {
        const auto offset = (beat - beatStart) / beatsPerSample;
        return static_cast<std::int64_t> (blockStartSample) + std::llround (offset);
    };

    const auto columnEndBeat = static_cast<double> (columnIndex + 1)
                               / static_cast<double> (looper::spectrumColumnsPerBeat);
    const auto endSample   = sampleForBeat (columnEndBeat);
    const auto startSample = endSample - looper::spectrumFftSize;

    SpectralColumn column;
    column.index = columnIndex;

    int samplesBudgeted = 0;
    bool anyData = false;

    if (startSample >= 0)
    {
        const int indices[] = { sourceLeft.load (std::memory_order_relaxed),
                                sourceRight.load (std::memory_order_relaxed) };
        const bool mono = indices[1] == indices[0];
        const auto sideGain = mono ? 1.0f : 0.5f;  // L/R-Mittel

        fftData.fill (0.0f);

        for (int side = 0; side < 2; ++side)
        {
            if (side == 1 && mono)
                break;

            // Budget zählt den VERSUCH (bindet die Backfill-Iterationen
            // pro Block auch über Gate-Löcher hinweg)
            samplesBudgeted += looper::spectrumFftSize;

            const auto* channel = capture.getAudioChannelView (indices[side]);
            if (channel == nullptr
                || ! channel->read (static_cast<std::uint64_t> (startSample),
                                    spectrumScratch.data(), looper::spectrumFftSize))
                continue;  // Loch/Gate zu/keine Quelle → Seite trägt nichts bei

            juce::FloatVectorOperations::addWithMultiply (fftData.data(), spectrumScratch.data(),
                                                          sideGain, looper::spectrumFftSize);
            anyData = true;
        }

        if (anyData)
        {
            // Hann-Fenster + Magnitude-FFT (Engine im Ctor gebaut +
            // warmgelaufen — perform ist allocation-free)
            juce::FloatVectorOperations::multiply (fftData.data(), window.data(),
                                                   looper::spectrumFftSize);
            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

            for (int b = 0; b < looper::spectrumBands; ++b)
            {
                const auto binLo = bands.edges[static_cast<std::size_t> (b)];
                const auto binHi = bands.edges[static_cast<std::size_t> (b + 1)];

                float peak = 0.0f;
                for (int bin = binLo; bin < binHi; ++bin)
                    peak = juce::jmax (peak, fftData[static_cast<std::size_t> (bin)]);

                column.bands[static_cast<std::size_t> (b)] = looper::spectrumLevel (peak);
            }
        }
    }
    // sonst: Null-Spalte (vor Signalbeginn bzw. Loch = Stille)

    spectrumQueue.push (column);  // voll → Spalte verfällt (UI holt 60×/s)
    return samplesBudgeted;
}

} // namespace conduit
