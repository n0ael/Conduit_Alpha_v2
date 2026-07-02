#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/Metronome.h"
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

int firstNonZeroSample (const juce::AudioBuffer<float>& buffer, int channel)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        if (std::abs (buffer.getSample (channel, i)) > 1.0e-6f)
            return i;

    return -1;
}

} // namespace

//==============================================================================
TEST_CASE ("Metronome: Click startet sample-genau an der Beat-Grenze", "[metronome]")
{
    conduit::Metronome metronome;
    metronome.prepare (testSampleRate);
    metronome.setEnabled (true);

    const auto beatsPerSample = 120.0 / (60.0 * testSampleRate);

    juce::AudioBuffer<float> buffer (2, 64);

    // Vorlauf-Block etabliert die Beat-Verfolgung (erster Block triggert
    // bewusst nicht rückwirkend)
    buffer.clear();
    metronome.process (buffer, 2, makeClock (0.9));

    // Beat-Grenze 1.0 liegt exakt auf Sample 32 dieses Blocks
    const auto blockStart = 1.0 - 32.0 * beatsPerSample;
    buffer.clear();
    metronome.process (buffer, 2, makeClock (blockStart));

    REQUIRE (firstNonZeroSample (buffer, 0) == 32);
    REQUIRE (firstNonZeroSample (buffer, 1) == 32);  // Stereo-Paar
}

TEST_CASE ("Metronome: nur die Anker-Kanäle bekommen den Click", "[metronome]")
{
    conduit::Metronome metronome;
    metronome.prepare (testSampleRate);
    metronome.setEnabled (true);
    metronome.setAnchor (1);  // Kanäle 2/3 (z. B. Headphones)

    const auto beatsPerSample = 120.0 / (60.0 * testSampleRate);
    juce::AudioBuffer<float> buffer (4, 64);

    buffer.clear();
    metronome.process (buffer, 4, makeClock (0.9));
    buffer.clear();
    metronome.process (buffer, 4, makeClock (1.0 - 16.0 * beatsPerSample));

    REQUIRE (firstNonZeroSample (buffer, 0) == -1);
    REQUIRE (firstNonZeroSample (buffer, 1) == -1);
    REQUIRE (firstNonZeroSample (buffer, 2) == 16);
    REQUIRE (firstNonZeroSample (buffer, 3) == 16);
}

TEST_CASE ("Metronome: deaktiviert bleibt still, Tail klingt aus (kein Knacks)", "[metronome]")
{
    conduit::Metronome metronome;
    metronome.prepare (testSampleRate);

    const auto beatsPerSample = 120.0 / (60.0 * testSampleRate);
    juce::AudioBuffer<float> buffer (2, 64);

    // Deaktiviert: nie ein Sample
    buffer.clear();
    metronome.process (buffer, 2, makeClock (0.99));
    buffer.clear();
    metronome.process (buffer, 2, makeClock (1.0 - 8.0 * beatsPerSample));
    REQUIRE (firstNonZeroSample (buffer, 0) == -1);

    // Aktivieren, Click triggern, dann deaktivieren: der Tail läuft weiter,
    // aber es kommt kein NEUER Trigger
    metronome.setEnabled (true);
    buffer.clear();
    metronome.process (buffer, 2, makeClock (2.0 - 8.0 * beatsPerSample));
    REQUIRE (firstNonZeroSample (buffer, 0) == 8);

    metronome.setEnabled (false);
    buffer.clear();
    metronome.process (buffer, 2, makeClock (2.0 + 56.0 * beatsPerSample));
    REQUIRE (firstNonZeroSample (buffer, 0) == 0);  // Tail direkt am Blockanfang

    // Nach ~1 s Stille ist auch der Tail weg — und die nächste Beat-Grenze
    // triggert nicht mehr
    for (int block = 0; block < 800; ++block)
    {
        buffer.clear();
        metronome.process (buffer, 2, makeClock (3.0 + block * 64.0 * beatsPerSample));
    }

    buffer.clear();
    metronome.process (buffer, 2, makeClock (5000.0));
    REQUIRE (firstNonZeroSample (buffer, 0) == -1);
}

TEST_CASE ("Metronome: Anker außerhalb der Kanalzahl schreibt nichts (kein OOB)", "[metronome]")
{
    conduit::Metronome metronome;
    metronome.prepare (testSampleRate);
    metronome.setEnabled (true);
    metronome.setAnchor (7);  // Kanäle 14/15 — Buffer hat nur 2

    juce::AudioBuffer<float> buffer (2, 64);
    buffer.clear();
    metronome.process (buffer, 2, makeClock (0.99));
    buffer.clear();
    metronome.process (buffer, 2, makeClock (1.0));

    REQUIRE (firstNonZeroSample (buffer, 0) == -1);
    REQUIRE (firstNonZeroSample (buffer, 1) == -1);
}

TEST_CASE ("Metronome: process ist allocation-free (RT-Audit)", "[metronome]")
{
    conduit::Metronome metronome;
    metronome.prepare (testSampleRate);
    metronome.setEnabled (true);

    juce::AudioBuffer<float> buffer (2, 64);
    buffer.clear();

    const auto violationsBefore = conduit::rt::getAllocationViolations();

    {
        const conduit::rt::ScopedRealtimeSection rtAudit;

        for (int block = 0; block < 32; ++block)
            metronome.process (buffer, 2, makeClock (0.9 + block * 0.01));
    }

    if (conduit::rt::isHookActive())
        REQUIRE (conduit::rt::getAllocationViolations() == violationsBefore);
}
