#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/CurveEditInteraction.h"

namespace grid = conduit::grid;
using Catch::Approx;
using Target = grid::CurveEditInteraction::Target;

//==============================================================================
TEST_CASE ("CurveEditInteraction: hitTest erkennt Endpunkte und die leere Mitte", "[grid]")
{
    constexpr float outMin = 0.0f;
    constexpr float outMax = 1.0f;
    constexpr float hitRadius = 0.15f;

    // Nahe (0, outMin) -> MinEndpoint
    REQUIRE (grid::CurveEditInteraction::hitTest ({ 0.02f, 0.03f }, outMin, outMax, hitRadius)
             == Target::MinEndpoint);

    // Nahe (1, outMax) -> MaxEndpoint
    REQUIRE (grid::CurveEditInteraction::hitTest ({ 0.98f, 0.97f }, outMin, outMax, hitRadius)
             == Target::MaxEndpoint);

    // Mitte des Feldes -> Curvature
    REQUIRE (grid::CurveEditInteraction::hitTest ({ 0.5f, 0.5f }, outMin, outMax, hitRadius)
             == Target::Curvature);

    // Außerhalb des Trefferradius beider Endpunkte -> Curvature
    REQUIRE (grid::CurveEditInteraction::hitTest ({ 0.3f, 0.3f }, outMin, outMax, hitRadius)
             == Target::Curvature);
}

TEST_CASE ("CurveEditInteraction: hitTest funktioniert auch bei invertierter Kurve (outMin > outMax)", "[grid]")
{
    constexpr float outMin = 0.9f;   // Endpunkt (0, 0.9) -- oben im Feld
    constexpr float outMax = 0.1f;   // Endpunkt (1, 0.1) -- unten im Feld
    constexpr float hitRadius = 0.15f;

    REQUIRE (grid::CurveEditInteraction::hitTest ({ 0.0f, 0.9f }, outMin, outMax, hitRadius)
             == Target::MinEndpoint);
    REQUIRE (grid::CurveEditInteraction::hitTest ({ 1.0f, 0.1f }, outMin, outMax, hitRadius)
             == Target::MaxEndpoint);
    REQUIRE (grid::CurveEditInteraction::hitTest ({ 0.5f, 0.5f }, outMin, outMax, hitRadius)
             == Target::Curvature);
}

TEST_CASE ("CurveEditInteraction: endpointValueFromY klemmt auf [0,1]", "[grid]")
{
    REQUIRE (grid::CurveEditInteraction::endpointValueFromY (0.0f) == Approx (0.0f));
    REQUIRE (grid::CurveEditInteraction::endpointValueFromY (1.0f) == Approx (1.0f));
    REQUIRE (grid::CurveEditInteraction::endpointValueFromY (0.5f) == Approx (0.5f));
    REQUIRE (grid::CurveEditInteraction::endpointValueFromY (1.5f) == Approx (1.0f));
    REQUIRE (grid::CurveEditInteraction::endpointValueFromY (-0.5f) == Approx (0.0f));
}

TEST_CASE ("CurveEditInteraction: curvatureDelta -- Richtung und Proportionalität zur sensitivity", "[grid]")
{
    // Wisch nach oben (current < start) -> positives Delta
    REQUIRE (grid::CurveEditInteraction::curvatureDelta (0.5f, 0.3f, 1.0f) == Approx (0.2f));

    // Wisch nach unten (current > start) -> negatives Delta
    REQUIRE (grid::CurveEditInteraction::curvatureDelta (0.3f, 0.5f, 1.0f) == Approx (-0.2f));

    // Proportional zur sensitivity
    REQUIRE (grid::CurveEditInteraction::curvatureDelta (0.5f, 0.3f, 2.0f) == Approx (0.4f));

    // Keine Bewegung -> kein Delta
    REQUIRE (grid::CurveEditInteraction::curvatureDelta (0.5f, 0.5f, 1.5f) == Approx (0.0f));
}
