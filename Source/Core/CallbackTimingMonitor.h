#pragma once

#include <atomic>
#include <cstdint>

#include <juce_core/juce_core.h>

namespace conduit
{

//==============================================================================
/**
    Callback-Timing-Diagnose [Audio → UI] — trennt „mein PC" von „mein Code".

    Misst pro Audio-Callback zwei Größen (Feld-Lektion 04.07.2026: 32-Sample-
    Buffer riss Deadlines → Beat-Achsen-Rutscher → Looper-Re-Syncs; dieser
    Zähler macht die Vorstufe sichtbar, BEVOR der Looper re-syncen muss):

      - XRun: Abstand zwischen den STARTS aufeinanderfolgender Callbacks.
        Überschreitet er das Doppelte der Blockdauer, kam der Callback einen
        ganzen Block zu spät — der Treiber musste überbrücken (Deadline-Riss).
        Lange Stalls (Debugger, Device-Suspend) zählen bewusst als EIN XRun.
      - Load: Rechenzeit des Blocks relativ zum Budget (numSamples/sampleRate)
        in Promille; die UI konsumiert den Peak seit dem letzten Abruf
        (peak-hold, consumePeakLoadPermille).

    Wall-Clock im Audio-Thread: bewusste, dokumentierte Ausnahme zu CLAUDE.md
    3.1 — getHighResolutionTicks (QueryPerformanceCounter) ist lock- und
    allocation-frei und dient AUSSCHLIESSLICH der Diagnose, nie als Zeitbasis
    fürs Rendering (die bleibt SampleClock/ClockState).

    Threading: beginBlock/endBlock [Audio Thread]; prepare [MT, Audio steht];
    Getter/consume [Message Thread] über Atomics. noteBlock ist die pure,
    tick-injizierte Kernlogik für Tests.
*/
class CallbackTimingMonitor
{
public:
    /** Gap > xrunGapFactor × Blockdauer ⇒ Deadline-Riss (ein voller Block
        Verspätung — normales Scheduling-Jitter bleibt weit darunter). */
    static constexpr double xrunGapFactor = 2.0;

    /** [MT, Audio steht] Neue Rate, Zähler auf null (frische Diagnose wie
        LooperEngine::prepare → snapCount). */
    void prepare (double newSampleRate) noexcept
    {
        sampleRate     = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        lastStartTicks = 0;
        startTicks     = 0;
        xruns.store (0, std::memory_order_relaxed);
        peakLoadPermille.store (0, std::memory_order_relaxed);
    }

    /** [Audio] Erste Zeile des Callbacks. */
    void beginBlock() noexcept
    {
        startTicks = juce::Time::getHighResolutionTicks();
    }

    /** [Audio] Letzte Zeile des Callbacks. */
    void endBlock (int numSamples) noexcept
    {
        noteBlock (startTicks, juce::Time::getHighResolutionTicks(), numSamples,
                   juce::Time::getHighResolutionTicksPerSecond());
    }

    /** Kernlogik, tick-injiziert [Tests treiben sie direkt]. */
    void noteBlock (std::int64_t blockStartTicks, std::int64_t blockEndTicks,
                    int numSamples, std::int64_t ticksPerSecond) noexcept
    {
        if (numSamples <= 0 || ticksPerSecond <= 0)
            return;

        const auto blockSeconds = static_cast<double> (numSamples) / sampleRate;

        // Gap-Check erst ab dem zweiten Block (lastStartTicks == 0 = frisch
        // prepared; Tick 0 selbst kommt von QPC praktisch nie vor)
        if (lastStartTicks != 0)
        {
            const auto gapSeconds = static_cast<double> (blockStartTicks - lastStartTicks)
                                  / static_cast<double> (ticksPerSecond);
            if (gapSeconds > blockSeconds * xrunGapFactor)
                xruns.fetch_add (1, std::memory_order_relaxed);
        }
        lastStartTicks = blockStartTicks;

        const auto loadSeconds = static_cast<double> (blockEndTicks - blockStartTicks)
                               / static_cast<double> (ticksPerSecond);
        const auto permille = static_cast<std::uint32_t> (
            juce::jlimit (0.0, 65535.0, loadSeconds / blockSeconds * 1000.0));

        // store-if-greater (CAS): einziger Schreiber ist der Audio-Thread,
        // aber consumePeakLoadPermille tauscht nebenläufig auf 0
        auto previous = peakLoadPermille.load (std::memory_order_relaxed);
        while (previous < permille
               && ! peakLoadPermille.compare_exchange_weak (previous, permille,
                                                            std::memory_order_relaxed)) {}
    }

    /** [MT] Deadline-Risse seit prepare. */
    [[nodiscard]] std::uint32_t getXrunCount() const noexcept
    {
        return xruns.load (std::memory_order_relaxed);
    }

    /** [MT] Peak-Load (Promille des Block-Budgets) seit dem letzten Abruf —
        liest UND nullt (peak-hold pro UI-Tick). */
    [[nodiscard]] std::uint32_t consumePeakLoadPermille() noexcept
    {
        return peakLoadPermille.exchange (0, std::memory_order_relaxed);
    }

private:
    // [MT, Audio steht] geschrieben, [Audio] gelesen — Kontrakt wie die
    // übrigen prepare-Member des EngineProcessor
    double sampleRate = 48000.0;

    // nur Audio-Thread
    std::int64_t lastStartTicks = 0;
    std::int64_t startTicks     = 0;

    // Audio → UI
    std::atomic<std::uint32_t> xruns            { 0 };
    std::atomic<std::uint32_t> peakLoadPermille { 0 };
};

} // namespace conduit
