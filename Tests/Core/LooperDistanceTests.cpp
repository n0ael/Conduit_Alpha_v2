#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Der Header unter Test ist JUCE-frei; juce_core kommt nur fuer
// exactlyEqual dazu (Clang-CI: -Wfloat-equal).
#include <juce_core/juce_core.h>

#include "Core/Looper/LooperDistance.h"

using Catch::Approx;
namespace ld = conduit::looper;

namespace
{
constexpr double sr = 48000.0;

// Frequenzgang eines Biquads bei Frequenz f (Betrag) via z = e^{jw}
double magnitudeAt (const ld::BiquadCoeffs& c, double freqHz)
{
    const auto w = 2.0 * 3.141592653589793 * freqHz / sr;
    const auto cos1 = std::cos (w),  sin1 = std::sin (w);
    const auto cos2 = std::cos (2.0 * w), sin2 = std::sin (2.0 * w);

    const auto numRe = c.b0 + c.b1 * cos1 + c.b2 * cos2;
    const auto numIm = -(c.b1 * sin1 + c.b2 * sin2);
    const auto denRe = 1.0 + c.a1 * cos1 + c.a2 * cos2;
    const auto denIm = -(c.a1 * sin1 + c.a2 * sin2);

    return std::sqrt ((numRe * numRe + numIm * numIm)
                    / (denRe * denRe + denIm * denIm));
}
} // namespace

//==============================================================================
TEST_CASE ("LooperDistance: Mappings sind monoton und treffen die Endpunkte", "[looper]")
{
    SECTION ("Tiefpass-Cutoff: offen bei d=0, hiCut bei d=1, monoton fallend")
    {
        REQUIRE (ld::lowpassCutoffHz (0.0, 8000.0, sr) == Approx (20000.0));
        REQUIRE (ld::lowpassCutoffHz (1.0, 8000.0, sr) == Approx (8000.0));

        auto previous = ld::lowpassCutoffHz (0.0, 2000.0, sr);
        for (double d = 0.1; d <= 1.0; d += 0.1)
        {
            const auto cutoff = ld::lowpassCutoffHz (d, 2000.0, sr);
            REQUIRE (cutoff < previous);
            previous = cutoff;
        }
    }

    SECTION ("Shelf-Gain: 0 dB bei d=0, −hiDump bei d=1")
    {
        REQUIRE (ld::shelfGainDb (0.0, 18.0) == Approx (0.0));
        REQUIRE (ld::shelfGainDb (1.0, 18.0) == Approx (-18.0));
        REQUIRE (ld::shelfGainDb (0.5, 12.0) == Approx (-6.0));
    }

    SECTION ("Width-Faktor: 1 bei d=0, width01 bei d=1")
    {
        REQUIRE (ld::widthFactor (0.0, 0.4) == Approx (1.0));
        REQUIRE (ld::widthFactor (1.0, 0.4) == Approx (0.4));
        REQUIRE (ld::widthFactor (1.0, 0.0) == Approx (0.0));
    }

    SECTION ("Vol-Kurve: aus = 1; d=0 → 1; d=1 → EXAKT 0 (Stille)")
    {
        REQUIRE (ld::volDumpGain (0.7, false, 12.0) == Approx (1.0));
        REQUIRE (ld::volDumpGain (0.0, true, 12.0) == Approx (1.0));
        REQUIRE (juce::exactlyEqual (ld::volDumpGain (1.0, true, 12.0), 0.0));
        REQUIRE (juce::exactlyEqual (ld::volDumpGain (1.0, true, 0.0), 0.0));

        // monoton fallend
        auto previous = 1.0;
        for (double d = 0.1; d <= 0.9; d += 0.1)
        {
            const auto gain = ld::volDumpGain (d, true, 12.0);
            REQUIRE (gain < previous);
            previous = gain;
        }
    }
}

TEST_CASE ("LooperDistance: RBJ-Biquads — DC-/HF-Verhalten", "[looper]")
{
    SECTION ("Tiefpass: DC-Gain 1, Nyquist stark gedämpft, −3 dB am Cutoff")
    {
        const auto c = ld::makeLowpass (2000.0, sr);
        REQUIRE (magnitudeAt (c, 10.0) == Approx (1.0).margin (0.01));
        REQUIRE (magnitudeAt (c, 20000.0) < 0.02);
        REQUIRE (magnitudeAt (c, 2000.0) == Approx (0.70710678).margin (0.02));
    }

    SECTION ("High-Shelf: DC-Gain 1, HF-Gain = 10^(dB/20)")
    {
        const auto c = ld::makeHighShelf (2000.0, -12.0, sr);
        REQUIRE (magnitudeAt (c, 10.0) == Approx (1.0).margin (0.01));
        REQUIRE (magnitudeAt (c, 20000.0)
                 == Approx (ld::dbToGain (-12.0)).margin (0.01));
    }

    SECTION ("Biquad-Schritt: Unity-Koeffizienten = Passthrough")
    {
        ld::BiquadState state;
        const ld::BiquadCoeffs unity;   // b0=1, Rest 0
        REQUIRE (juce::exactlyEqual (ld::processBiquad (state, unity, 0.5f), 0.5f));
        REQUIRE (juce::exactlyEqual (ld::processBiquad (state, unity, -0.25f), -0.25f));
    }
}
