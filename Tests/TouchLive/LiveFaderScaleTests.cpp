#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TouchLive/LiveFaderScale.h"

using Catch::Approx;
namespace scale = conduit::touchlive::faderscale;

//==============================================================================
TEST_CASE ("LiveFaderScale: Anker — Unity 0.85 → 0 dB, Vollausschlag → +6 dB", "[touchlive]")
{
    REQUIRE (scale::dbFromValue (scale::unityValue) == Approx (0.0).margin (1e-9));
    REQUIRE (scale::dbFromValue (1.0) == Approx (6.0));
    REQUIRE (scale::valueFromDb (0.0) == Approx (scale::unityValue));
    REQUIRE (scale::valueFromDb (6.0) == Approx (1.0));
    REQUIRE (scale::dbFromValue (0.0) == Approx (scale::silenceDb));
    REQUIRE (scale::valueFromDb (scale::silenceDb - 10.0) == Approx (0.0));
}

TEST_CASE ("LiveFaderScale: Roundtrip und Monotonie über den ganzen Bereich", "[touchlive]")
{
    double previousDb = scale::silenceDb - 1.0;

    for (int step = 1; step <= 100; ++step)
    {
        const auto value = (double) step / 100.0;
        const auto db = scale::dbFromValue (value);

        REQUIRE (db > previousDb);   // streng monoton
        previousDb = db;

        REQUIRE (scale::valueFromDb (db) == Approx (value).margin (1e-6));
    }
}

TEST_CASE ("LiveFaderScale: dB-Text", "[touchlive]")
{
    REQUIRE (scale::dbText (scale::unityValue) == "0.0");
    REQUIRE (scale::dbText (0.0) == "-inf");
}
