#include <vector>

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
TEST_CASE ("LevelMeter: Clip Auto-Clear via setClipHoldSeconds", "[levelmeter][io]")
{
    LevelMeter meter;
    meter.prepare (testSampleRate, 1);

    SECTION ("Hold 0 (Default) → Latch bleibt")
    {
        feedConstant (meter, 1, 1.0f, 1);
        feedConstant (meter, 1, 0.0f, 200);  // 2 s Stille
        REQUIRE (meter.isClipped (0));
    }

    SECTION ("Hold 0,5 s → Latch verlischt nach Ablauf")
    {
        meter.setClipHoldSeconds (0.5f);
        feedConstant (meter, 1, 1.0f, 1);
        REQUIRE (meter.isClipped (0));

        feedConstant (meter, 1, 0.0f, 40);   // 0,4 s < 0,5 → noch gesetzt
        REQUIRE (meter.isClipped (0));

        feedConstant (meter, 1, 0.0f, 20);   // gesamt 0,6 s > 0,5 → gelöscht
        REQUIRE_FALSE (meter.isClipped (0));
    }

    SECTION ("erneutes Clippen setzt den Auto-Clear-Timer zurück")
    {
        meter.setClipHoldSeconds (0.5f);
        feedConstant (meter, 1, 1.0f, 1);
        feedConstant (meter, 1, 0.0f, 40);   // 0,4 s
        feedConstant (meter, 1, 1.0f, 1);    // erneuter Clip → Timer reset
        feedConstant (meter, 1, 0.0f, 40);   // wieder nur 0,4 s → noch gesetzt
        REQUIRE (meter.isClipped (0));
    }
}

//==============================================================================
TEST_CASE ("LevelMeter: processPointers — rohe Kanal-Pointer + Stille-Zweig",
           "[levelmeter][io][looper]")
{
    LevelMeter meter;
    meter.prepare (testSampleRate, 3);

    std::vector<float> loud (testBlockSize, 0.8f);
    std::vector<float> hot  (testBlockSize, 1.0f);

    SECTION ("misst pro Pointer-Kanal wie process(), nullptr = Stille")
    {
        const float* channels[3] = { loud.data(), nullptr, hot.data() };
        meter.processPointers (channels, 3, testBlockSize);

        REQUIRE (meter.getPeak (0) == Approx (0.8f));
        REQUIRE (meter.getRms (0) == Approx (0.8f).margin (0.01));
        REQUIRE (meter.getPeak (1) == Approx (0.0f));
        REQUIRE (meter.isClipped (2));
        REQUIRE_FALSE (meter.isClipped (1));
    }

    SECTION ("nullptr lässt die Ballistik abfallen (Peak-Release wie Stille)")
    {
        const float* feed[3] = { loud.data(), loud.data(), nullptr };
        meter.processPointers (feed, 3, testBlockSize);
        REQUIRE (meter.getPeak (1) == Approx (0.8f));

        const float* silent[3] = { nullptr, nullptr, nullptr };
        for (int b = 0; b < 300; ++b)   // 3 s Stille (RMS: sqrt halbiert die dB-Rate)
            meter.processPointers (silent, 3, testBlockSize);

        REQUIRE (meter.getPeak (1) < 0.1f);
        REQUIRE (meter.getRms (1) < 0.05f);
    }

    SECTION ("Kanäle jenseits activeChannels werden ignoriert")
    {
        const float* channels[3] = { loud.data(), loud.data(), loud.data() };
        meter.prepare (testSampleRate, 2);
        meter.processPointers (channels, 3, testBlockSize);
        REQUIRE (meter.getPeak (2) == Approx (0.0f));
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
