#include "CurveEditInteraction.h"

#include <cmath>

namespace conduit::grid
{

CurveEditInteraction::Target CurveEditInteraction::hitTest (juce::Point<float> normPos,
                                                            float outMin, float outMax,
                                                            float hitRadius,
                                                            const ResponseCurve& curve) noexcept
{
    // Endpunkte haben Vorrang (bestehende Regel: Endpunkt schlaegt alles).
    const auto endpointTarget = hitTest (normPos, outMin, outMax, hitRadius);
    if (endpointTarget != Target::Curvature)
        return endpointTarget;

    // Mittelpunkt (Block C): nur bei der 3-Punkt-Kurve vorhanden. Handle-
    // Position in Feld-Koordinaten = (mid.x, outMin + mid.y*(outMax-outMin)).
    if (curve.numPoints() == 3)
    {
        const auto& mid = curve.points()[1];
        const juce::Point<float> handle { mid.x, outMin + mid.y * (outMax - outMin) };

        if (normPos.getDistanceFrom (handle) <= hitRadius)
            return Target::MidPoint;
    }

    return Target::Curvature;
}

CurveEditInteraction::Target CurveEditInteraction::hitTest (juce::Point<float> normPos,
                                                            float outMin, float outMax,
                                                            float hitRadius) noexcept
{
    const juce::Point<float> minPoint { 0.0f, outMin };
    const juce::Point<float> maxPoint { 1.0f, outMax };

    const auto distToMin = normPos.getDistanceFrom (minPoint);
    const auto distToMax = normPos.getDistanceFrom (maxPoint);

    Target nearest = Target::MaxEndpoint;
    auto nearestDist = distToMax;

    if (distToMin < distToMax)
    {
        nearest = Target::MinEndpoint;
        nearestDist = distToMin;
    }
    else if (juce::exactlyEqual (distToMin, distToMax))
    {
        // Gleichstand: horizontal näherer Endpunkt gewinnt (Min liegt bei
        // x=0, Max bei x=1).
        const auto dxMin = std::abs (normPos.x - minPoint.x);
        const auto dxMax = std::abs (normPos.x - maxPoint.x);
        nearest     = dxMin <= dxMax ? Target::MinEndpoint : Target::MaxEndpoint;
        nearestDist = juce::jmin (distToMin, distToMax);
    }

    return nearestDist <= hitRadius ? nearest : Target::Curvature;
}

float CurveEditInteraction::endpointValueFromY (float normY) noexcept
{
    return juce::jlimit (0.0f, 1.0f, normY);
}

float CurveEditInteraction::curvatureDelta (float startNormY, float currentNormY,
                                            float sensitivity) noexcept
{
    return (startNormY - currentNormY) * sensitivity;
}

//==============================================================================
// Mehrpunkt-Kurve (Block C)

int CurveEditInteraction::curvatureSegmentAt (const ResponseCurve& curve, float normX) noexcept
{
    if (curve.numPoints() != 3)
        return 0;

    return normX <= curve.points()[1].x ? 0 : 1;
}

float CurveEditInteraction::rotationDegrees (juce::Point<float> aStart, juce::Point<float> bStart,
                                             juce::Point<float> aNow, juce::Point<float> bNow) noexcept
{
    const auto startVec = bStart - aStart;
    const auto nowVec   = bNow - aNow;

    const auto startAngle = std::atan2 (startVec.y, startVec.x);
    const auto nowAngle   = std::atan2 (nowVec.y, nowVec.x);

    auto delta = nowAngle - startAngle;

    // Auf [-pi, pi] entfalten -- die kuerzere Drehrichtung zaehlt.
    while (delta > juce::MathConstants<float>::pi)
        delta -= juce::MathConstants<float>::twoPi;
    while (delta < -juce::MathConstants<float>::pi)
        delta += juce::MathConstants<float>::twoPi;

    return juce::radiansToDegrees (delta);
}

float CurveEditInteraction::degreesToShapeAmount (float degrees) noexcept
{
    // Vorzeichen per User-Gefuehlstest 11.07.: Drehung GEGEN den
    // Uhrzeigersinn (positive Grad, y nach oben) = positive Bauchigkeit.
    return juce::jlimit (-1.0f, 1.0f, degrees / kFullShapeDegrees);
}

void CurveEditInteraction::applyShapeAmount (ResponseCurve& curve, float amount,
                                             float restoreCurvature) noexcept
{
    const auto first = curve.points().front();
    const auto last  = curve.points().back();

    if (std::abs (amount) < kShapeDeadZone)
    {
        // Zurueck in der Null-Lage: Mittelpunkt verschwindet, die Kurve
        // wird wieder 2-Punkt mit der Kruemmung von vor der Geste.
        if (curve.numPoints() != 2)
            curve.setPoints ({ first, last });

        curve.setSegmentCurvature (0, restoreCurvature);
        return;
    }

    // Mittelpunkt sicherstellen -- eine bereits (per Drag) verschobene
    // Mitte bleibt stehen, nur die Bauchigkeit aendert sich.
    if (curve.numPoints() != 3)
        curve.setPoints ({ first, { 0.5f, 0.5f }, last });

    curve.setSegmentCurvature (0, amount);
    curve.setSegmentCurvature (1, -amount);
}

void CurveEditInteraction::applyMidPointDrag (ResponseCurve& curve, juce::Point<float> normPos,
                                              float outMin, float outMax) noexcept
{
    if (curve.numPoints() != 3)
        return;

    const auto range = outMax - outMin;
    const auto y = std::abs (range) > 1.0e-6f
                       ? juce::jlimit (0.0f, 1.0f, (normPos.y - outMin) / range)
                       : 0.5f;
    const auto x = juce::jlimit (kMidPointMinX, kMidPointMaxX, normPos.x);

    const auto first = curve.points().front();
    const auto last  = curve.points().back();

    // setPoints erhaelt die vorhandenen Segment-Kruemmungen (resize).
    curve.setPoints ({ first, { x, y }, last });
}

void CurveEditInteraction::resetToDefault (ResponseCurve& curve) noexcept
{
    curve.setPoints ({ { 0.0f, 0.0f }, { 1.0f, 1.0f } });
    curve.setSegmentCurvature (0, 0.0f);   // setPoints erhaelt alte c0 -- explizit nullen
    curve.setOutputRange (0.0f, 1.0f);
}

} // namespace conduit::grid
