#include <catch2/catch_test_macros.hpp>

#include "Core/CallbackTimingMonitor.h"

using conduit::CallbackTimingMonitor;

//==============================================================================
// Die Kernlogik (noteBlock) ist tick-injiziert und damit deterministisch
// testbar — beginBlock/endBlock sind nur dünne QPC-Wrapper.
//
// Testbasis: 48 kHz, 128-Sample-Blöcke, Ticks in Mikrosekunden.
// Blockdauer = 128/48000 s ≈ 2667 µs; XRun-Schwelle = 2 × Blockdauer.

namespace
{
    constexpr std::int64_t ticksPerSecond = 1'000'000;  // µs-Ticks
    constexpr int          blockSamples   = 128;
    constexpr std::int64_t blockMicros    = 2667;       // ≈ 128/48000 s
}

TEST_CASE ("CallbackTimingMonitor: regelmäßige Callbacks erzeugen keine XRuns",
           "[timing]")
{
    CallbackTimingMonitor monitor;
    monitor.prepare (48000.0);

    // 100 Blöcke im Soll-Raster, Rechenzeit je 500 µs (~19 % Load)
    std::int64_t start = 1000;
    for (int i = 0; i < 100; ++i)
    {
        monitor.noteBlock (start, start + 500, blockSamples, ticksPerSecond);
        start += blockMicros;
    }

    CHECK (monitor.getXrunCount() == 0);

    // Peak-Load: 500/2667 ≈ 187 ‰ (Rundung der Tick-Mathematik tolerieren)
    const auto peak = monitor.consumePeakLoadPermille();
    CHECK (peak >= 180);
    CHECK (peak <= 195);
}

TEST_CASE ("CallbackTimingMonitor: Gap über doppelter Blockdauer zählt als XRun",
           "[timing]")
{
    CallbackTimingMonitor monitor;
    monitor.prepare (48000.0);

    std::int64_t start = 1000;
    monitor.noteBlock (start, start + 200, blockSamples, ticksPerSecond);

    // Nächster Callback kommt 3 Blockdauern später → Deadline gerissen
    start += 3 * blockMicros;
    monitor.noteBlock (start, start + 200, blockSamples, ticksPerSecond);
    CHECK (monitor.getXrunCount() == 1);

    // danach wieder im Raster → kein weiterer XRun
    for (int i = 0; i < 10; ++i)
    {
        start += blockMicros;
        monitor.noteBlock (start, start + 200, blockSamples, ticksPerSecond);
    }
    CHECK (monitor.getXrunCount() == 1);
}

TEST_CASE ("CallbackTimingMonitor: erster Block nach prepare zählt nie als XRun",
           "[timing]")
{
    CallbackTimingMonitor monitor;
    monitor.prepare (48000.0);

    // Beliebig späte erste Ticks — es gibt keinen Vorgänger-Start
    monitor.noteBlock (5'000'000'000LL, 5'000'000'400LL, blockSamples, ticksPerSecond);
    CHECK (monitor.getXrunCount() == 0);
}

TEST_CASE ("CallbackTimingMonitor: consumePeakLoadPermille ist peak-hold und nullt",
           "[timing]")
{
    CallbackTimingMonitor monitor;
    monitor.prepare (48000.0);

    // drei Blöcke mit Load ~19 %, ~75 %, ~37 % → Peak = ~75 %
    std::int64_t start = 1000;
    monitor.noteBlock (start, start + 500,  blockSamples, ticksPerSecond); start += blockMicros;
    monitor.noteBlock (start, start + 2000, blockSamples, ticksPerSecond); start += blockMicros;
    monitor.noteBlock (start, start + 1000, blockSamples, ticksPerSecond); start += blockMicros;

    const auto peak = monitor.consumePeakLoadPermille();
    CHECK (peak >= 740);
    CHECK (peak <= 760);

    // konsumiert → 0, bis der nächste Block schreibt
    CHECK (monitor.consumePeakLoadPermille() == 0);

    monitor.noteBlock (start, start + 500, blockSamples, ticksPerSecond);
    CHECK (monitor.consumePeakLoadPermille() > 0);
}

TEST_CASE ("CallbackTimingMonitor: Überlast wird geclampt statt überzulaufen",
           "[timing]")
{
    CallbackTimingMonitor monitor;
    monitor.prepare (48000.0);

    // Rechenzeit 10 s auf 2,7-ms-Budget → Load-Promille am Clamp (65535)
    monitor.noteBlock (1000, 1000 + 10 * ticksPerSecond, blockSamples, ticksPerSecond);
    CHECK (monitor.consumePeakLoadPermille() == 65535);
}

TEST_CASE ("CallbackTimingMonitor: prepare setzt Zähler und Gap-Basis zurück",
           "[timing]")
{
    CallbackTimingMonitor monitor;
    monitor.prepare (48000.0);

    std::int64_t start = 1000;
    monitor.noteBlock (start, start + 500, blockSamples, ticksPerSecond);
    monitor.noteBlock (start + 5 * blockMicros, start + 5 * blockMicros + 500,
                       blockSamples, ticksPerSecond);
    REQUIRE (monitor.getXrunCount() == 1);

    // Re-prepare (Gerätewechsel): frischer Zähler, und der erste Block
    // danach darf trotz riesiger Tick-Distanz keinen XRun zählen
    monitor.prepare (48000.0);
    CHECK (monitor.getXrunCount() == 0);
    CHECK (monitor.consumePeakLoadPermille() == 0);

    monitor.noteBlock (start + 1'000'000'000LL, start + 1'000'000'500LL,
                       blockSamples, ticksPerSecond);
    CHECK (monitor.getXrunCount() == 0);
}

TEST_CASE ("CallbackTimingMonitor: ungültige Eingaben sind No-ops", "[timing]")
{
    CallbackTimingMonitor monitor;
    monitor.prepare (48000.0);

    monitor.noteBlock (1000, 2000, 0, ticksPerSecond);   // numSamples 0
    monitor.noteBlock (1000, 2000, blockSamples, 0);     // ticksPerSecond 0
    CHECK (monitor.getXrunCount() == 0);
    CHECK (monitor.consumePeakLoadPermille() == 0);
}
