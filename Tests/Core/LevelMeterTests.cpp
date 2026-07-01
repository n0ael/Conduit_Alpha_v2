#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/Capture/LevelMeter.h"

using Catch::Approx;
using conduit::LevelMeter;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr int testBlockSize = 480;  // 10 ms — 100 Blöcke/s

// Speist N Blöcke mit konstanter Amplitude in allen Kanälen.
void feedConstant (LevelMeter& meter, int numChannels, float value, int blocks)
{
    juce::AudioBuffer<float> buffer (numChannels, testBlockSize);
    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), value, testBlockSize);
        meter.process (buffer, numChannels);
    }
}

} // namespace

//==============================================================================
TEST_CASE ("LevelMeter: Peak-Attack und RMS-Konvergenz", "[levelmeter][io]")
{
    LevelMeter meter;
    meter.prepare (testSampleRate, 2);

    SECTION ("Peak folgt sofort dem Blockmaximum")
    {
        feedConstant (meter, 2, 0.8f, 1);
        REQUIRE (meter.getPeak (0) == Approx (0.8f));
        REQUIRE (meter.getPeak (1) == Approx (0.8f));
    }

    SECTION ("RMS eines Konstantsignals entspricht der Amplitude (Warm-Start)")
    {
        feedConstant (meter, 2, 0.5f, 1);  // ein Block reicht dank Warm-Start
        REQUIRE (meter.getRms (0) == Approx (0.5f).margin (0.01));
    }
}

//==============================================================================
TEST_CASE ("LevelMeter: Peak-Release-Ballistik", "[levelmeter][io]")
{
    LevelMeter meter;
    meter.prepare (testSampleRate, 1);

    feedConstant (meter, 1, 0.9f, 1);
    REQUIRE (meter.getPeak (0) == Approx (0.9f));

    SECTION ("fällt nicht sofort")
    {
        feedConstant (meter, 1, 0.0f, 1);  // 10 ms Stille
        REQUIRE (meter.getPeak (0) > 0.85f);
    }

    SECTION ("nach ~1,5 s deutlich gefallen, aber > 0")
    {
        feedConstant (meter, 1, 0.0f, 150);  // 1,5 s Stille
        REQUIRE (meter.getPeak (0) < 0.5f);
        REQUIRE (meter.getPeak (0) > 0.0f);
    }

    SECTION ("nach langer Stille nahe 0")
    {
        feedConstant (meter, 1, 0.0f, 800);  // 8 s Stille
        REQUIRE (meter.getPeak (0) < 0.01f);
    }
}

//==============================================================================
TEST_CASE ("LevelMeter: Peak-Hold hält länger als der Momentan-Peak", "[levelmeter][io]")
{
    LevelMeter meter;
    meter.prepare (testSampleRate, 1);

    feedConstant (meter, 1, 0.9f, 1);       // Transient
    feedConstant (meter, 1, 0.0f, 100);     // 1 s Stille (< Haltezeit 1,5 s)

    REQUIRE (meter.getPeakHold (0) == Approx (0.9f));   // hält noch
    REQUIRE (meter.getPeakHold (0) > meter.getPeak (0)); // über dem gefallenen Peak

    // Nach Ablauf der Haltezeit fällt auch der Hold-Wert
    feedConstant (meter, 1, 0.0f, 300);     // weitere 3 s
    REQUIRE (meter.getPeakHold (0) < 0.9f);
}

//==============================================================================
TEST_CASE ("LevelMeter: Clip-Latch und Reset", "[levelmeter][io]")
{
    LevelMeter meter;
    meter.prepare (testSampleRate, 2);

    SECTION ("Latch bei 0 dBFS, bleibt nach Stille gesetzt")
    {
        feedConstant (meter, 2, 1.0f, 1);   // genau 0 dBFS
        REQUIRE (meter.isClipped (0));
        REQUIRE (meter.isClipped (1));

        feedConstant (meter, 2, 0.0f, 50);  // Stille löscht das Latch NICHT
        REQUIRE (meter.isClipped (0));
    }

    SECTION ("knapp unter 0 dBFS clippt nicht")
    {
        feedConstant (meter, 2, 0.99f, 5);
        REQUIRE_FALSE (meter.isClipped (0));
    }

    SECTION ("resetClip löscht nur den betroffenen Kanal")
    {
        feedConstant (meter, 2, 1.0f, 1);
        meter.resetClip (0);
        REQUIRE_FALSE (meter.isClipped (0));
        REQUIRE (meter.isClipped (1));
    }
}

//==============================================================================
TEST_CASE ("LevelMeter: Out-of-range-Kanäle liefern Nullwerte", "[levelmeter][io]")
{
    LevelMeter meter;
    meter.prepare (testSampleRate, 2);
    feedConstant (meter, 2, 1.0f, 1);

    REQUIRE (meter.getNumActiveChannels() == 2);
    REQUIRE (meter.getPeak (5)  == Approx (0.0f));
    REQUIRE (meter.getRms (5)   == Approx (0.0f));
    REQUIRE_FALSE (meter.isClipped (5));
    meter.resetClip (5);  // No-op, kein Absturz
}
