#include <atomic>
#include <cstdint>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "Core/Looper/BarSampleAnchors.h"
#include "Util/RtAllocationGuard.h"

namespace
{

constexpr double testSampleRate = 48000.0;

conduit::ClockState makeClock (double beatAtBlockStart, double bpm = 120.0)
{
    conduit::ClockState clock;
    clock.bpm = bpm;
    clock.beatAtBlockStart = beatAtBlockStart;
    clock.sampleRate = testSampleRate;
    return clock;
}

/** Treibt die Anker wie der EngineProcessor: Beat-Achse und SampleClock
    laufen synchron über beliebige Blockgrößen; Tempo pro Block wählbar. */
struct AnchorRig
{
    conduit::BarSampleAnchors anchors;
    double beat = 0.0;
    std::uint64_t samplePos = 0;

    void feed (int numSamples, double bpm = 120.0)
    {
        anchors.process (makeClock (beat, bpm), samplePos, numSamples);
        beat += (bpm / (60.0 * testSampleRate)) * numSamples;
        samplePos += static_cast<std::uint64_t> (numSamples);
    }
};

} // namespace

//==============================================================================
TEST_CASE ("BarSampleAnchors: Taktgrenzen sample-genau über krumme Blockgrößen", "[looper]")
{
    // 120 BPM @ 48 kHz: 1 Beat = 24 000 Samples, 1 Takt = 96 000 Samples
    AnchorRig rig;
    REQUIRE (rig.anchors.latestBoundaryBar() == -1);

    // Absichtlich unregelmäßige Blockgrößen — die Grenzen fallen mitten
    // in die Blöcke
    const int blockSizes[] = { 480, 333, 512, 1, 4096, 64, 1000 };
    int sizeIndex = 0;
    while (rig.samplePos < 400'000)  // > 4 Takte
        rig.feed (blockSizes[sizeIndex++ % 7]);

    REQUIRE (rig.anchors.latestBoundaryBar() == 4);

    for (std::int64_t bar = 1; bar <= 4; ++bar)
    {
        std::uint64_t sample = 0;
        REQUIRE (rig.anchors.lookup (bar, sample));
        REQUIRE (sample == static_cast<std::uint64_t> (bar) * 96'000);
    }

    // Grenze 0 (Session-Start) wird nie überquert — Beat 0 ist der Anfang
    std::uint64_t unused = 0;
    REQUIRE_FALSE (rig.anchors.lookup (0, unused));
    REQUIRE_FALSE (rig.anchors.lookup (-1, unused));
    REQUIRE_FALSE (rig.anchors.lookup (5, unused));
}

TEST_CASE ("BarSampleAnchors: Tempo-Wechsel verschiebt nur künftige Grenzen", "[looper]")
{
    AnchorRig rig;

    // Bis exakt Takt 2 bei 120 BPM (Sample 192 000, Beat 8)
    while (rig.samplePos < 192'000)
        rig.feed (480);

    // Ab hier 96 BPM: 1 Beat = 30 000 Samples → Grenze 3 (Beat 12) liegt
    // 4 Beats × 30 000 = 120 000 Samples später. (Fällt Grenze 2 durch
    // fp-Rundung erst in den ersten 96-BPM-Block, bleibt ihre Position
    // trotzdem exakt 192 000 — der Intra-Block-Offset rundet auf 0.)
    while (rig.samplePos < 192'000 + 121'000)
        rig.feed (333, 96.0);

    std::uint64_t sample = 0;
    REQUIRE (rig.anchors.lookup (3, sample));
    REQUIRE (sample == 192'000 + 120'000);

    // Der Anker VOR dem Wechsel ist exakt (kein Extrapolieren darüber hinweg)
    REQUIRE (rig.anchors.lookup (2, sample));
    REQUIRE (sample == 192'000);
}

TEST_CASE ("BarSampleAnchors: Grenze exakt auf Blockrand ankert genau einmal", "[looper]")
{
    AnchorRig rig;

    // Blöcke à 24 000: Takt 1 (Beat 4) fällt exakt auf das Ende von Block 4.
    // Je nach fp-Rundung ankert die Grenze im Block davor (Offset ==
    // numSamples) oder im Folgeblock (Offset 0) — die Position muss in
    // BEIDEN Fällen exakt 96 000 sein und stabil bleiben.
    for (int i = 0; i < 4; ++i)
        rig.feed (24'000);
    rig.feed (480);

    std::uint64_t sample = 0;
    REQUIRE (rig.anchors.lookup (1, sample));
    REQUIRE (sample == 96'000);

    // Weitere Blöcke ändern den Anker nicht mehr
    rig.feed (480);
    REQUIRE (rig.anchors.lookup (1, sample));
    REQUIRE (sample == 96'000);
}

TEST_CASE ("BarSampleAnchors: Slot-Reuse nach capacity Takten — Lookup-Miss statt Falschwert", "[looper]")
{
    AnchorRig rig;

    // 20 Takte anfahren — Slot 4 (= 20 % 16) trägt jetzt Takt 20
    while (rig.anchors.latestBoundaryBar() < 20)
        rig.feed (4096);

    std::uint64_t sample = 0;
    REQUIRE (rig.anchors.lookup (20, sample));
    REQUIRE (sample == 20 * 96'000);

    // Takt 4 wurde überschrieben → sauberer Miss
    REQUIRE_FALSE (rig.anchors.lookup (4, sample));

    // Takt 5 (Slot 5, noch nicht wiederverwendet) ist weiter exakt
    REQUIRE (rig.anchors.lookup (5, sample));
    REQUIRE (sample == 5 * 96'000);
}

TEST_CASE ("BarSampleAnchors: Beat-Sprung ankert nicht rückwirkend", "[looper]")
{
    AnchorRig rig;

    while (rig.samplePos < 100'000)  // Takt 1 geankert
        rig.feed (480);
    REQUIRE (rig.anchors.latestBoundaryBar() == 1);

    // Peer-Join: die Beat-Achse springt weit nach vorn — die übersprungenen
    // Takte existieren in der SampleClock nicht und dürfen nie ankern
    rig.beat = 100.5;
    rig.feed (480);
    REQUIRE (rig.anchors.latestBoundaryBar() == 1);

    std::uint64_t sample = 0;
    REQUIRE_FALSE (rig.anchors.lookup (25, sample));  // Beat 100 = Grenze 25

    // Die NÄCHSTE Grenze nach dem Sprung (Takt 26, Beat 104) ankert normal
    while (rig.beat < 104.5)
        rig.feed (480);
    REQUIRE (rig.anchors.latestBoundaryBar() == 26);
    REQUIRE (rig.anchors.lookup (26, sample));
}

TEST_CASE ("BarSampleAnchors: reset verwirft alles", "[looper]")
{
    AnchorRig rig;
    while (rig.anchors.latestBoundaryBar() < 2)
        rig.feed (4096);

    rig.anchors.reset();
    REQUIRE (rig.anchors.latestBoundaryBar() == -1);

    std::uint64_t sample = 0;
    REQUIRE_FALSE (rig.anchors.lookup (1, sample));
    REQUIRE_FALSE (rig.anchors.lookup (2, sample));
}

TEST_CASE ("BarSampleAnchors: process ist allocation-free (RT-Audit)", "[looper]")
{
    AnchorRig rig;

    const auto violationsBefore = conduit::rt::getAllocationViolations();

    {
        const conduit::rt::ScopedRealtimeSection rtAudit;

        for (int block = 0; block < 256; ++block)
            rig.feed (480);
    }

    if (conduit::rt::isHookActive())
        REQUIRE (conduit::rt::getAllocationViolations() == violationsBefore);
}

//==============================================================================
TEST_CASE ("BarSampleAnchors: nebenläufige Lookups liefern nie Falschwerte (Stress)", "[looper][stress]")
{
    // Invariante bei konstantem Tempo: Grenze b liegt EXAKT bei b × 96 000.
    // Ein Lookup, der true liefert, muss diese Position exakt treffen —
    // egal wie Writer und Reader interleaved laufen (TSan-Pflicht via CI).
    conduit::BarSampleAnchors anchors;
    std::atomic<bool> stop { false };
    std::atomic<std::int64_t> hits { 0 };
    std::atomic<bool> corrupt { false };

    std::thread reader ([&]
    {
        while (! stop.load (std::memory_order_relaxed))
        {
            const auto latest = anchors.latestBoundaryBar();

            // Absichtlich auch Slots am Reuse-Rand abfragen
            for (std::int64_t bar = latest - 20; bar <= latest + 1; ++bar)
            {
                std::uint64_t sample = 0;
                if (anchors.lookup (bar, sample))
                {
                    hits.fetch_add (1, std::memory_order_relaxed);
                    if (sample != static_cast<std::uint64_t> (bar) * 96'000)
                        corrupt.store (true, std::memory_order_relaxed);
                }
            }
        }
    });

    // Writer = Audio-Thread-Surrogat auf dem Test-Thread: ~200 Takte
    // in kleinen Blöcken bei konstantem Tempo
    double beat = 0.0;
    std::uint64_t samplePos = 0;
    const auto beatsPerSample = 120.0 / (60.0 * testSampleRate);

    while (samplePos < 200ULL * 96'000ULL)
    {
        anchors.process (makeClock (beat), samplePos, 480);
        beat += beatsPerSample * 480;
        samplePos += 480;
    }

    stop.store (true, std::memory_order_relaxed);
    reader.join();

    REQUIRE_FALSE (corrupt.load());
    REQUIRE (hits.load() > 0);
    REQUIRE (anchors.latestBoundaryBar() >= 199);  // fp-Rand: 200. Grenze evtl. knapp vorm Blockende
}
