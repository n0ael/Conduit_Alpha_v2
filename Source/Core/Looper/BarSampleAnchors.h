#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

#include <juce_core/juce_core.h>

#include "Interfaces/IClockSource.h"
#include "LooperMath.h"

namespace conduit
{

//==============================================================================
/**
    Sample-genaue Takt-Anker der Session-Beat-Achse (Looper-Baustein B1).

    Problem: der ClockState liefert nur beatAtBlockStart pro Block — wer
    einen VERGANGENEN Takt sample-exakt im Capture-Ring adressieren will
    (Looper-Commit "die letzten N Takte"), darf nicht über Tempo-Wechsel
    hinweg extrapolieren. Lösung: der Audio Thread erkennt Takt-
    Überquerungen sample-genau (floor-Muster 4.5, wie Metronome) und
    publiziert pro Grenze das Paar {barIndex, absolute SampleClock-Position}.

    Anker-Semantik: Grenze b liegt bei Beat b·quantumBeats — der ANFANG von
    Takt b und das ENDE von Takt b−1. Ein Commit der letzten N Takte liest
    [lookup(latest − N), lookup(latest)).

    16-Slot-Ring (Slot = bar % capacity): deckt die letzten ≥ 16 Takt-
    grenzen — mehr als das 8-Takt-Sichtfenster je braucht. Pro Slot liegt
    das Paar in EINEM gepackten 64-bit-Atomic (obere 16 Bit = bar & 0xFFFF
    als Tag, untere 48 Bit = Sample-Position): ein einziger Load, kein
    Torn-Read-Fenster beim Slot-Reuse (der Stress-Test hat die naive
    Zwei-Atomics-Variante gebrochen: neue Position sichtbar, alter Takt
    noch nicht). Grenzen der Packung — beide praktisch unerreichbar:
    48-bit-Position trägt @192 kHz ~46 Jahre Session; das 16-bit-Tag
    kollidiert erst, wenn ein Lookup 65536 Takte daneben fragt.

    Threading: process() NUR Audio Thread (einziger Writer); lookup() und
    latestBoundaryBar() von jedem Thread (Message Thread beim Commit).
    reset() nur bei stehendem Audio (prepareToPlay — die SampleClock
    resettet dort, alle früheren Positionen sind ungültig).
*/
class BarSampleAnchors
{
public:
    static constexpr int capacity = 16;

    BarSampleAnchors() { resetSlots(); }

    /** [Message Thread, Audio steht — prepareToPlay] */
    void reset() noexcept
    {
        resetSlots();
        latest.store (-1, std::memory_order_release);
        previousBeat = 0.0;
        beatValid = false;
    }

    /** [Audio Thread, einmal pro Block NACH dem ClockBus-Fill]
        blockStartSample = SampleClock-Stand des ersten Samples dieses
        Blocks (die Clock tickt am Tap-Ende: now() − numSamples,
        Kontrakt CaptureService.h). Allocation-free. */
    void process (const ClockState& clock, std::uint64_t blockStartSample,
                  int numSamples) noexcept
    {
        const auto beatsPerSample = clock.beatsPerSample();
        if (numSamples <= 0 || beatsPerSample <= 0.0)
            return;

        const auto beatStart = clock.beatAtBlockStart;
        const auto beatEnd   = beatStart + beatsPerSample * numSamples;

        // Sprung in der Beat-Achse (Peer-Join, Offset-Änderung, erster
        // Block): nicht rückwirkend ankern — Muster Metronome
        if (! beatValid || std::abs (beatStart - previousBeat) > 1.0)
            previousBeat = beatStart;

        beatValid = true;

        auto bar = static_cast<std::int64_t> (
                       std::floor (previousBeat / looper::quantumBeats)) + 1;
        const auto lastBar = static_cast<std::int64_t> (
                                 std::floor (beatEnd / looper::quantumBeats));

        for (; bar <= lastBar; ++bar)
        {
            if (bar < 0)
                continue;  // negative Session-Beats (Clock-Offset): nie ankern

            // Intra-Block-Offset der Grenze — sample-genau, geclampt gegen
            // minimalen Jitter der Beat-Achse an den Blockrändern
            const auto offset = std::clamp (
                (static_cast<double> (bar) * looper::quantumBeats - beatStart)
                    / beatsPerSample,
                0.0, static_cast<double> (numSamples));

            const auto samplePos = blockStartSample
                                   + static_cast<std::uint64_t> (std::llround (offset));

            slots[static_cast<std::size_t> (bar % capacity)]
                .store (pack (bar, samplePos), std::memory_order_release);
            latest.store (bar, std::memory_order_release);
        }

        previousBeat = beatEnd;
    }

    /** [beliebiger Thread] Absolute SampleClock-Position der Taktgrenze —
        false, wenn (noch) nicht geankert oder älter als capacity Takte. */
    [[nodiscard]] bool lookup (std::int64_t boundaryBar,
                               std::uint64_t& samplePosOut) const noexcept
    {
        if (boundaryBar < 0)
            return false;

        const auto packed = slots[static_cast<std::size_t> (boundaryBar % capacity)]
                                .load (std::memory_order_acquire);

        if ((packed >> sampleBits) != barTag (boundaryBar))
            return false;

        samplePosOut = packed & sampleMask;
        return true;
    }

    /** [beliebiger Thread] Jüngste geankerte Taktgrenze, −1 = noch keine. */
    [[nodiscard]] std::int64_t latestBoundaryBar() const noexcept
    {
        return latest.load (std::memory_order_acquire);
    }

private:
    static constexpr int sampleBits = 48;
    static constexpr std::uint64_t sampleMask = (1ULL << sampleBits) - 1;

    [[nodiscard]] static constexpr std::uint64_t barTag (std::int64_t bar) noexcept
    {
        return static_cast<std::uint64_t> (bar) & 0xFFFFULL;
    }

    [[nodiscard]] static constexpr std::uint64_t pack (std::int64_t bar,
                                                       std::uint64_t samplePos) noexcept
    {
        return (barTag (bar) << sampleBits) | (samplePos & sampleMask);
    }

    /** Leere Slots tragen ein Tag, das KEIN Takt dieses Slots je haben kann:
        Slot s hält nur Takte ≡ s (mod capacity), das Sentinel-Tag s+1 ist
        dazu inkongruent (capacity teilt 2^16) — kein False-Positive vor dem
        ersten Anker. */
    void resetSlots() noexcept
    {
        for (std::size_t s = 0; s < slots.size(); ++s)
            slots[s].store (static_cast<std::uint64_t> (s + 1) << sampleBits,
                            std::memory_order_relaxed);
    }

    static_assert (std::atomic<std::int64_t>::is_always_lock_free
                       && std::atomic<std::uint64_t>::is_always_lock_free,
                   "BarSampleAnchors-Atomics müssen lock-free sein (Audio Thread)");

    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t> (capacity)> slots;
    std::atomic<std::int64_t> latest { -1 };

    // Nur Audio Thread
    double previousBeat = 0.0;
    bool   beatValid    = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BarSampleAnchors)
};

} // namespace conduit
