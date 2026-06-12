#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "Core/Capture/CaptureService.h"

using Catch::Approx;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr int testBlockSize = 480;  // 10 ms — 100 Blöcke pro Sekunde

/** Füllt einen Kanal mit einem phasenkontinuierlichen Sinus. */
void fillSine (juce::AudioBuffer<float>& buffer, int channel,
               float amplitude, double frequency, double& phase)
{
    auto* data = buffer.getWritePointer (channel);
    const double phaseInc = juce::MathConstants<double>::twoPi * frequency / testSampleRate;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        data[i] = amplitude * static_cast<float> (std::sin (phase));
        phase += phaseInc;
    }
}

/** Füllt einen Kanal mit gleichverteiltem Rauschen in [-amplitude, amplitude].
    RMS von gleichverteiltem Rauschen: amplitude / sqrt(3). */
void fillNoise (juce::AudioBuffer<float>& buffer, int channel,
                float amplitude, juce::Random& rng)
{
    auto* data = buffer.getWritePointer (channel);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        data[i] = amplitude * (rng.nextFloat() * 2.0f - 1.0f);
}

} // namespace

//==============================================================================
TEST_CASE ("SampleClock: monoton, resetbar, ignoriert nicht-positive Schritte", "[capture]")
{
    conduit::SampleClock clock;
    REQUIRE (clock.now() == 0);

    clock.advance (480);
    clock.advance (32);
    REQUIRE (clock.now() == 512);

    clock.advance (0);
    clock.advance (-5);  // defensiv: keine Rückwärtsbewegung
    REQUIRE (clock.now() == 512);

    clock.reset();  // Samplerate-Wechsel invalidiert alle Positionen
    REQUIRE (clock.now() == 0);
}

//==============================================================================
TEST_CASE ("InputMeter: RMS und Peak eines Sinus bekannter Amplitude", "[capture]")
{
    conduit::InputMeter meter;
    meter.prepare (testSampleRate, 2);
    REQUIRE (meter.getNumActiveChannels() == 2);

    constexpr float amplitude = 0.5f;
    juce::AudioBuffer<float> buffer (2, testBlockSize);
    double phase = 0.0;

    // 1 s Sinus auf Kanal 0, Stille auf Kanal 1 — das 50-ms-RMS-Fenster
    // ist danach längst eingeschwungen
    for (int block = 0; block < 100; ++block)
    {
        buffer.clear();
        fillSine (buffer, 0, amplitude, 440.0, phase);
        meter.process (buffer, 2);
    }

    const double expectedRms = amplitude / std::sqrt (2.0);
    REQUIRE (meter.getRms (0)  == Approx (expectedRms).epsilon (0.02));
    REQUIRE (meter.getPeak (0) == Approx (amplitude).epsilon (0.02));

    REQUIRE (meter.getRms (1)  == Approx (0.0).margin (1.0e-6));
    REQUIRE (meter.getPeak (1) == Approx (0.0).margin (1.0e-6));

    // Außerhalb des aktiven Bereichs: definierte 0.0f, kein UB
    REQUIRE (juce::exactlyEqual (meter.getRms (-1), 0.0f));
    REQUIRE (juce::exactlyEqual (meter.getRms (2),  0.0f));
    REQUIRE (juce::exactlyEqual (meter.getRms (conduit::MAX_CAPTURE_CHANNELS), 0.0f));
}

//==============================================================================
TEST_CASE ("InputMeter: Noise-Floor konvergiert auf den Rauschpegel", "[capture]")
{
    conduit::InputMeter meter;
    meter.prepare (testSampleRate, 1);

    juce::AudioBuffer<float> buffer (1, testBlockSize);
    juce::Random rng (42);

    constexpr float noiseAmp = 0.01f;
    const double trueNoiseRms = noiseAmp / std::sqrt (3.0);  // ~0.00577

    // 5 s Rauschen → Floor sitzt auf dem Rauschpegel (Minimum-Tracking
    // liest die RMS-Fluktuation leicht nach unten — Toleranz nach unten)
    for (int block = 0; block < 500; ++block)
    {
        fillNoise (buffer, 0, noiseAmp, rng);
        meter.process (buffer, 1);
    }

    const float floorAfterNoise = meter.getNoiseFloor (0);
    REQUIRE (floorAfterNoise > 0.85 * trueNoiseRms);
    REQUIRE (floorAfterNoise < 1.05 * trueNoiseRms);

    // 1 s lautes Signal (RMS ~0.35): Floor steigt nur sanft (30-s-Release)
    // und bleibt weit unter dem Signalpegel
    double phase = 0.0;
    for (int block = 0; block < 100; ++block)
    {
        fillSine (buffer, 0, 0.5f, 440.0, phase);
        meter.process (buffer, 1);
    }

    const float floorAfterLoud = meter.getNoiseFloor (0);
    REQUIRE (floorAfterLoud >= floorAfterNoise);  // gestiegen ...
    REQUIRE (floorAfterLoud < 0.05f);             // ... aber Signal zieht ihn nicht hoch

    // Ruhe → Floor schnappt sofort wieder auf den Rauschpegel zurück
    for (int block = 0; block < 50; ++block)
    {
        fillNoise (buffer, 0, noiseAmp, rng);
        meter.process (buffer, 1);
    }

    REQUIRE (meter.getNoiseFloor (0) < 0.008f);
}

//==============================================================================
TEST_CASE ("CaptureService: Input-Tap taktet die SampleClock und misst", "[capture]")
{
    conduit::CaptureService service;
    service.prepare (testSampleRate, testBlockSize, 2);
    REQUIRE (service.getSampleClock().now() == 0);

    constexpr float amplitude = 0.25f;
    juce::AudioBuffer<float> buffer (2, testBlockSize);
    double phase = 0.0;

    for (int block = 0; block < 100; ++block)
    {
        buffer.clear();
        fillSine (buffer, 0, amplitude, 220.0, phase);
        service.processInputTap (buffer, 2);
    }

    REQUIRE (service.getSampleClock().now() == 100ull * static_cast<std::uint64_t> (testBlockSize));
    REQUIRE (service.getInputMeter().getRms (0)
             == Approx (amplitude / std::sqrt (2.0)).epsilon (0.02));

    // Kanalzahl jenseits von Buffer/MAX_CAPTURE_CHANNELS: geclamped, kein Crash
    service.processInputTap (buffer, 128);
    REQUIRE (service.getSampleClock().now() == 101ull * static_cast<std::uint64_t> (testBlockSize));

    // prepare() resettet die Clock — Samplerate-Wechsel invalidiert Positionen
    service.prepare (44100.0, testBlockSize, 2);
    REQUIRE (service.getSampleClock().now() == 0);
}
