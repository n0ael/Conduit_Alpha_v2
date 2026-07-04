#include "LooperWaveformTap.h"

#include <cmath>

namespace conduit
{

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

} // namespace conduit
