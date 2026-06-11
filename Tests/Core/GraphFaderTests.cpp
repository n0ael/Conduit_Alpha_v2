#include <catch2/catch_test_macros.hpp>

#include "Core/GraphFader.h"

namespace
{

void fillWithOnes (juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill (buffer.getWritePointer (channel),
                                           1.0f, buffer.getNumSamples());
}

constexpr int blockSize = 32;              // Latenz-Ziel aus CLAUDE.md 3.2
constexpr double testSampleRate = 48000.0; // nicht "sampleRate" — verschattet JUCE-Header-Parameter (C4459)
// 5ms Rampe @ 48kHz = 240 Samples = 7,5 Blöcke à 32
constexpr int maxBlocksForRamp = 12;

} // namespace

//==============================================================================
TEST_CASE ("GraphFader: unprepared ist er ein No-Op", "[GraphFader]")
{
    conduit::GraphFader fader;
    juce::AudioBuffer<float> buffer (2, blockSize);
    fillWithOnes (buffer);

    fader.beginFadeOut();
    fader.process (buffer);

    CHECK_FALSE (fader.isPrepared());
    CHECK (juce::exactlyEqual (buffer.getSample (0, blockSize - 1), 1.0f));
}

//==============================================================================
TEST_CASE ("GraphFader: Fade-Out erreicht Stille in ~5ms und meldet fadeComplete",
           "[GraphFader]")
{
    conduit::GraphFader fader;
    fader.prepare (testSampleRate);

    juce::AudioBuffer<float> buffer (2, blockSize);

    // Active: Signal bleibt unangetastet
    fillWithOnes (buffer);
    fader.process (buffer);
    REQUIRE (juce::exactlyEqual (buffer.getSample (0, blockSize - 1), 1.0f));

    // Schritt 2: Fade-Out
    fader.beginFadeOut();
    REQUIRE_FALSE (fader.isFadeOutComplete());

    int blocks = 0;
    while (! fader.isFadeOutComplete() && blocks < 100)
    {
        fillWithOnes (buffer);
        fader.process (buffer);
        ++blocks;
    }

    REQUIRE (fader.isFadeOutComplete());
    CHECK (blocks <= maxBlocksForRamp);

    // Ab fadeComplete ist jeder weitere Block garantiert still —
    // darauf verlässt sich der Topologie-Swap (Schritt 3).
    fillWithOnes (buffer);
    fader.process (buffer);
    CHECK (juce::exactlyEqual (buffer.getMagnitude (0, blockSize), 0.0f));
}

//==============================================================================
TEST_CASE ("GraphFader: Fade-In rampt zurück auf 1.0 und kehrt zu Active zurück",
           "[GraphFader]")
{
    conduit::GraphFader fader;
    fader.prepare (testSampleRate);

    juce::AudioBuffer<float> buffer (2, blockSize);

    fader.beginFadeOut();
    for (int i = 0; i < 100 && ! fader.isFadeOutComplete(); ++i)
    {
        fillWithOnes (buffer);
        fader.process (buffer);
    }
    REQUIRE (fader.isFadeOutComplete());

    // Schritt 4: Fade-In
    fader.beginFadeIn();
    REQUIRE (fader.getCurrentPhase() == conduit::GraphFader::Phase::fadingIn);

    int blocks = 0;
    while (fader.getCurrentPhase() != conduit::GraphFader::Phase::active && blocks < 100)
    {
        fillWithOnes (buffer);
        fader.process (buffer);
        ++blocks;
    }

    CHECK (fader.getCurrentPhase() == conduit::GraphFader::Phase::active);
    CHECK (blocks <= maxBlocksForRamp);

    fillWithOnes (buffer);
    fader.process (buffer);
    CHECK (juce::exactlyEqual (buffer.getSample (0, blockSize - 1), 1.0f));
}
