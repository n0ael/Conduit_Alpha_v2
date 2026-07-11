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

//==============================================================================
// Mehrpunkt-Kurve (Block C)

TEST_CASE ("CurveEditInteraction: hitTest trifft den Mittelpunkt der 3-Punkt-Kurve", "[grid]")
{
    grid::ResponseCurve curve;
    grid::CurveEditInteraction::applyRotationShape (curve, true);   // -> 3 Punkte, Mitte (0.5, 0.5)

    // Direkt auf dem Griff -> MidPoint
    REQUIRE (grid::CurveEditInteraction::hitTest ({ 0.5f, 0.5f }, 0.0f, 1.0f, 0.1f, curve)
             == Target::MidPoint);

    // Weit weg von allem -> Curvature
    REQUIRE (grid::CurveEditInteraction::hitTest ({ 0.25f, 0.9f }, 0.0f, 1.0f, 0.1f, curve)
             == Target::Curvature);

    // Endpunkt schlaegt Mittelpunkt (bestehende Prioritaet)
    REQUIRE (grid::CurveEditInteraction::hitTest ({ 0.02f, 0.02f }, 0.0f, 1.0f, 0.1f, curve)
             == Target::MinEndpoint);
}

TEST_CASE ("CurveEditInteraction: hitTest kennt bei der 2-Punkt-Kurve keinen Mittelpunkt", "[grid]")
{
    grid::ResponseCurve curve;   // Default: 2 Punkte

    REQUIRE (grid::CurveEditInteraction::hitTest ({ 0.5f, 0.5f }, 0.0f, 1.0f, 0.1f, curve)
             == Target::Curvature);
}

TEST_CASE ("CurveEditInteraction: curvatureSegmentAt teilt links/rechts am Mittelpunkt", "[grid]")
{
    grid::ResponseCurve twoPoint;
    REQUIRE (grid::CurveEditInteraction::curvatureSegmentAt (twoPoint, 0.9f) == 0);

    grid::ResponseCurve threePoint;
    grid::CurveEditInteraction::applyRotationShape (threePoint, true);
    grid::CurveEditInteraction::applyMidPointDrag (threePoint, { 0.3f, 0.5f }, 0.0f, 1.0f);

    REQUIRE (grid::CurveEditInteraction::curvatureSegmentAt (threePoint, 0.1f) == 0);
    REQUIRE (grid::CurveEditInteraction::curvatureSegmentAt (threePoint, 0.6f) == 1);
}

TEST_CASE ("CurveEditInteraction: rotationDegrees -- Vorzeichen und Betrag", "[grid]")
{
    // Finger B rotiert um A von rechts (0 Grad) nach oben (+90, gegen den
    // Uhrzeigersinn in Feld-Koordinaten mit y nach oben).
    const auto ccw = grid::CurveEditInteraction::rotationDegrees (
        { 0.5f, 0.5f }, { 0.7f, 0.5f }, { 0.5f, 0.5f }, { 0.5f, 0.7f });
    REQUIRE (ccw == Approx (90.0f).margin (0.1));

    const auto cw = grid::CurveEditInteraction::rotationDegrees (
        { 0.5f, 0.5f }, { 0.7f, 0.5f }, { 0.5f, 0.5f }, { 0.5f, 0.3f });
    REQUIRE (cw == Approx (-90.0f).margin (0.1));

    // Keine Drehung -> ~0 (reine Translation beider Finger)
    const auto none = grid::CurveEditInteraction::rotationDegrees (
        { 0.2f, 0.2f }, { 0.4f, 0.2f }, { 0.3f, 0.5f }, { 0.5f, 0.5f });
    REQUIRE (none == Approx (0.0f).margin (0.1));
}

TEST_CASE ("CurveEditInteraction: applyRotationShape setzt 3 Punkte + gegensinnige Kruemmungen", "[grid]")
{
    grid::ResponseCurve curve;
    grid::CurveEditInteraction::applyRotationShape (curve, true);   // S-Kurve

    REQUIRE (curve.numPoints() == 3);
    REQUIRE (curve.points()[1].x == Approx (0.5f));
    REQUIRE (curve.points()[1].y == Approx (0.5f));

    // Uhrzeigersinn = steile Mitte (S: langsamer Start, schnelles Ende) --
    // shape(t, c>0) startet flach (ResponseCurve-Parametrierung).
    REQUIRE (curve.apply (0.25f) < 0.25f);
    REQUIRE (curve.apply (0.75f) > 0.75f);
    REQUIRE (curve.apply (0.5f) == Approx (0.5f).margin (1.0e-4));

    // Gegenrichtung = gespiegelte Form (flache Mitte, "?")
    grid::ResponseCurve mirrored;
    grid::CurveEditInteraction::applyRotationShape (mirrored, false);
    REQUIRE (mirrored.apply (0.25f) > 0.25f);
    REQUIRE (mirrored.apply (0.75f) < 0.75f);
}

TEST_CASE ("CurveEditInteraction: applyMidPointDrag verschiebt die Mitte, klemmt X und mappt Y ueber die Range", "[grid]")
{
    grid::ResponseCurve curve;
    grid::CurveEditInteraction::applyRotationShape (curve, true);

    grid::CurveEditInteraction::applyMidPointDrag (curve, { 0.3f, 0.8f }, 0.0f, 1.0f);
    REQUIRE (curve.points()[1].x == Approx (0.3f));
    REQUIRE (curve.points()[1].y == Approx (0.8f));
    REQUIRE (curve.points().front().x == Approx (0.0f));   // Endpunkte unangetastet
    REQUIRE (curve.points().back().x == Approx (1.0f));

    // X-Klemme
    grid::CurveEditInteraction::applyMidPointDrag (curve, { -1.0f, 0.5f }, 0.0f, 1.0f);
    REQUIRE (curve.points()[1].x == Approx (grid::CurveEditInteraction::kMidPointMinX));

    // Y wird ueber [outMin,outMax] zurueckgerechnet: Feld-y 0.5 bei Range
    // [0, 0.5] entspricht Punkt-y 1.0.
    grid::ResponseCurve ranged;
    grid::CurveEditInteraction::applyRotationShape (ranged, true);
    ranged.setOutputRange (0.0f, 0.5f);
    grid::CurveEditInteraction::applyMidPointDrag (ranged, { 0.5f, 0.5f }, 0.0f, 0.5f);
    REQUIRE (ranged.points()[1].y == Approx (1.0f));
}

TEST_CASE ("CurveEditInteraction: applyMidPointDrag ist bei der 2-Punkt-Kurve ein No-Op", "[grid]")
{
    grid::ResponseCurve curve;
    grid::CurveEditInteraction::applyMidPointDrag (curve, { 0.3f, 0.8f }, 0.0f, 1.0f);

    REQUIRE (curve.numPoints() == 2);
}

TEST_CASE ("CurveEditInteraction: resetToDefault stellt die 2-Punkt-Identitaet wieder her", "[grid]")
{
    grid::ResponseCurve curve;
    grid::CurveEditInteraction::applyRotationShape (curve, true);
    curve.setOutputRange (0.2f, 0.9f);

    grid::CurveEditInteraction::resetToDefault (curve);

    REQUIRE (curve.numPoints() == 2);
    REQUIRE (curve.getOutputMin() == Approx (0.0f));
    REQUIRE (curve.getOutputMax() == Approx (1.0f));
    for (const auto x : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        REQUIRE (curve.apply (x) == Approx (x).margin (1.0e-5));
}
