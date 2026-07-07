#include "CurveEditInteraction.h"

#include <cmath>

namespace conduit::grid
{

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

} // namespace conduit::grid
