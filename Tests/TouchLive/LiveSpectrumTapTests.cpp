#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TouchLive/LiveSpectrumTap.h"

using Catch::Approx;

namespace
{

/** Sinus-Blöcke in den Tap schieben (Audio-Thread-Pfad). */
void pushSine (conduit::LiveSpectrumTap& tap, double hz, double sampleRate,
               int numSamples, double amplitude = 1.0)
{
    std::vector<float> left ((size_t) numSamples), right ((size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto value = (float) (amplitude
            * std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate));
        left[(size_t) i] = value;
        right[(size_t) i] = value;
    }

    const float* channels[] = { left.data(), right.data() };
    tap.pushAudioBlock (channels, 2, numSamples);
}

} // namespace

TEST_CASE ("LiveSpectrumTap: Sinus landet im richtigen Bin (Input-Modus)",
           "[touchlive]")
{
    conduit::LiveSpectrumTap tap (nullptr);   // ohne LinkClock (Rule: Weak)
    tap.setAudioSampleRate (48000.0);
    tap.setMode (conduit::LiveSpectrumTap::SourceMode::audioInput);
    tap.setAveraging (0.0);                   // roh — ein Frame reicht

    REQUIRE (tap.getSourceLabel() == "Input");

    const auto binHz = 48000.0 / conduit::LiveSpectrumTap::fftSize;
    const auto targetBin = 40;
    pushSine (tap, targetBin * binHz, 48000.0, conduit::LiveSpectrumTap::fftSize + 512);

    const auto before = tap.getRevision();
    tap.analyseNow();
    REQUIRE (tap.getRevision() > before);

    const auto& magnitudes = tap.getMagnitudesDb();
    int peakBin = 0;

    for (int bin = 1; bin < conduit::LiveSpectrumTap::numBins; ++bin)
        if (magnitudes[(size_t) bin] > magnitudes[(size_t) peakBin])
            peakBin = bin;

    REQUIRE (peakBin == targetBin);
    REQUIRE (magnitudes[(size_t) peakBin] == Approx (0.0).margin (1.5));   // 0 dBFS
    REQUIRE (magnitudes[(size_t) targetBin + 200] < -60.0);                // Rauschboden
    REQUIRE (tap.isReceiving());
    REQUIRE (tap.binToHz (targetBin) == Approx (targetBin * binHz));
}

TEST_CASE ("LiveSpectrumTap: Averaging glättet, Moduswechsel leert", "[touchlive]")
{
    conduit::LiveSpectrumTap tap (nullptr);
    tap.setAudioSampleRate (48000.0);
    tap.setMode (conduit::LiveSpectrumTap::SourceMode::audioInput);
    tap.setAveraging (0.9);

    pushSine (tap, 1000.0, 48000.0, conduit::LiveSpectrumTap::fftSize + 512);
    tap.analyseNow();

    // Stark geglättet: nach EINEM Frame noch weit unter 0 dBFS
    const auto& magnitudes = tap.getMagnitudesDb();
    int peakBin = 0;

    for (int bin = 1; bin < conduit::LiveSpectrumTap::numBins; ++bin)
        if (magnitudes[(size_t) bin] > magnitudes[(size_t) peakBin])
            peakBin = bin;

    REQUIRE (magnitudes[(size_t) peakBin] < -20.0);

    // Aus-Modus: Anzeige zurückgesetzt, kein Empfang mehr
    tap.setMode (conduit::LiveSpectrumTap::SourceMode::off);
    REQUIRE_FALSE (tap.isReceiving());
    REQUIRE (tap.getMagnitudesDb()[(size_t) peakBin] == Approx (-90.0f));

    // Input-Pfad ist tot geschaltet (atomic gate)
    pushSine (tap, 1000.0, 48000.0, 1024);
    tap.analyseNow();
    REQUIRE (tap.getMagnitudesDb()[(size_t) peakBin] == Approx (-90.0f));
}
