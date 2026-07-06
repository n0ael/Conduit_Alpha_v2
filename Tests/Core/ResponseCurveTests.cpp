#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/ResponseCurve.h"

namespace grid = conduit::grid;
using Catch::Approx;

//==============================================================================
TEST_CASE ("ResponseCurve: Default ist die Identität", "[grid]")
{
    grid::ResponseCurve curve;

    REQUIRE (curve.apply (0.0f) == Approx (0.0f));
    REQUIRE (curve.apply (0.5f) == Approx (0.5f));
    REQUIRE (curve.apply (1.0f) == Approx (1.0f));
}

TEST_CASE ("ResponseCurve: setOutputRange skaliert den Ausgang, auch invertiert", "[grid]")
{
    grid::ResponseCurve curve;

    curve.setOutputRange (0.2f, 0.8f);
    REQUIRE (curve.apply (0.0f) == Approx (0.2f));
    REQUIRE (curve.apply (1.0f) == Approx (0.8f));
    REQUIRE (curve.apply (0.5f) == Approx (0.5f));

    curve.setOutputRange (1.0f, 0.0f);
    REQUIRE (curve.apply (0.0f) == Approx (1.0f));
    REQUIRE (curve.apply (1.0f) == Approx (0.0f));
}

TEST_CASE ("ResponseCurve: Mehrpunkt-Kurve trifft die Stützstellen, monoton steigend", "[grid]")
{
    grid::ResponseCurve curve;
    curve.setPoints ({ { 0.0f, 0.0f }, { 0.5f, 0.8f }, { 1.0f, 1.0f } });

    REQUIRE (curve.apply (0.5f) == Approx (0.8f));

    REQUIRE (curve.apply (0.0f)  < curve.apply (0.25f));
    REQUIRE (curve.apply (0.25f) < curve.apply (0.5f));
    REQUIRE (curve.apply (0.5f)  < curve.apply (0.75f));
    REQUIRE (curve.apply (0.75f) < curve.apply (1.0f));
}

TEST_CASE ("ResponseCurve: Segment-Krümmung formt zwischen den Stützstellen, Endpunkte unverändert", "[grid]")
{
    grid::ResponseCurve curve; // Default: ein Segment (0,0)->(1,1)

    curve.setSegmentCurvature (0, 1.0f); // konvex -- liegt unter dem linearen Wert
    REQUIRE (curve.apply (0.25f) < 0.25f);
    REQUIRE (curve.apply (0.0f) == Approx (0.0f));
    REQUIRE (curve.apply (1.0f) == Approx (1.0f));

    curve.setSegmentCurvature (0, -1.0f); // konkav -- liegt darüber
    REQUIRE (curve.apply (0.25f) > 0.25f);
    REQUIRE (curve.apply (0.0f) == Approx (0.0f));
    REQUIRE (curve.apply (1.0f) == Approx (1.0f));
}

TEST_CASE ("ResponseCurve: Eingänge außerhalb [0,1] werden linear extrapoliert", "[grid]")
{
    grid::ResponseCurve curve;

    REQUIRE (curve.apply (1.5f)  > curve.apply (1.0f));
    REQUIRE (curve.apply (-0.5f) < curve.apply (0.0f));
}

TEST_CASE ("ResponseCurve: setPoints erzwingt streng aufsteigende X, kein Crash bei unsortierten/gleichen Werten", "[grid]")
{
    grid::ResponseCurve curve;

    curve.setPoints ({ { 1.0f, 1.0f }, { 0.0f, 0.0f }, { 0.5f, 0.5f }, { 0.5f, 0.9f } });

    REQUIRE (curve.numSegments() == curve.numPoints() - 1);

    const auto& pts = curve.points();
    auto previousX = pts.front().x;

    for (std::size_t i = 1; i < pts.size(); ++i)
    {
        REQUIRE (pts[i].x > previousX);
        previousX = pts[i].x;
    }
}
