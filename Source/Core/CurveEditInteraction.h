#pragma once

#include <juce_graphics/juce_graphics.h>

namespace conduit::grid
{

/** Ordnet einen Touch im Kurvenfeld einem Editier-Ziel zu und rechnet
    Werte. Positionen normalisiert auf das Kurvenfeld: x∈[0,1] (0=links),
    y∈[0,1] (0=UNTEN, 1=oben). Endpunkte liegen bei (0, outMin) und
    (1, outMax). Headless. */
struct CurveEditInteraction
{
    enum class Target { None, MinEndpoint, MaxEndpoint, Curvature };

    /** Trefferzonen-Priorität: liegt der Touch innerhalb hitRadius um einen
        Endpunkt → dieser Endpunkt; sonst → Curvature. Bei gleichnahen
        Endpunkten gewinnt der horizontal nähere. */
    static Target hitTest (juce::Point<float> normPos,
                           float outMin, float outMax,
                           float hitRadius) noexcept;

    /** Endpunkt-Drag: neuer Output-Wert = normierte y-Position (0=unten→0.0,
        oben→1.0). Kein Clamp gegen den anderen Endpunkt (invertierte
        Min>Max-Kurven sind erlaubt). */
    static float endpointValueFromY (float normY) noexcept;

    /** Krümmungs-Wisch: Δcurvature aus vertikaler Bewegung
        (startNormY - currentNormY) · sensitivity; der Aufrufer addiert das
        auf die Krümmung bei Wisch-Beginn und clamped über setSegmentCurvature. */
    static float curvatureDelta (float startNormY, float currentNormY,
                                 float sensitivity) noexcept;
};

} // namespace conduit::grid
