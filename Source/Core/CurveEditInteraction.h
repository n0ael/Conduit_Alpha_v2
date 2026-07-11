#pragma once

#include <juce_graphics/juce_graphics.h>

#include "ResponseCurve.h"

namespace conduit::grid
{

/** Ordnet einen Touch im Kurvenfeld einem Editier-Ziel zu und rechnet
    Werte. Positionen normalisiert auf das Kurvenfeld: x∈[0,1] (0=links),
    y∈[0,1] (0=UNTEN, 1=oben). Endpunkte liegen bei (0, outMin) und
    (1, outMax); der Mittelpunkt (Block C, 3-Punkt-Kurve) bei
    (mid.x, outMin + mid.y·(outMax-outMin)). Headless. */
struct CurveEditInteraction
{
    enum class Target { None, MinEndpoint, MaxEndpoint, MidPoint, Curvature };

    /** Trefferzonen-Priorität: Endpunkte → Mittelpunkt (nur 3-Punkt-Kurve)
        → Curvature. Bei gleichnahen Endpunkten gewinnt der horizontal
        nähere; Endpunkt schlägt Mittelpunkt schlägt Krümmung. */
    static Target hitTest (juce::Point<float> normPos,
                           float outMin, float outMax,
                           float hitRadius) noexcept;

    /** Wie oben, berücksichtigt zusätzlich den Mittelpunkt der Kurve
        (Block C) -- bei 2-Punkt-Kurven identisch zur Überladung oben. */
    static Target hitTest (juce::Point<float> normPos,
                           float outMin, float outMax,
                           float hitRadius, const ResponseCurve& curve) noexcept;

    /** Endpunkt-Drag: neuer Output-Wert = normierte y-Position (0=unten→0.0,
        oben→1.0). Kein Clamp gegen den anderen Endpunkt (invertierte
        Min>Max-Kurven sind erlaubt). */
    static float endpointValueFromY (float normY) noexcept;

    /** Krümmungs-Wisch: Δcurvature aus vertikaler Bewegung
        (startNormY - currentNormY) · sensitivity; der Aufrufer addiert das
        auf die Krümmung bei Wisch-Beginn und clamped über setSegmentCurvature. */
    static float curvatureDelta (float startNormY, float currentNormY,
                                 float sensitivity) noexcept;

    //==========================================================================
    // Mehrpunkt-Kurve (Block C) -- alle Helfer headless, werden vom
    // MPE-Editor UND später 1:1 vom Macro-System (Block E) genutzt.

    /** Segment eines Krümmungs-Wischs: 2-Punkt-Kurve → immer 0; 3-Punkt-
        Kurve → 0 links vom Mittelpunkt (P1→Mitte), 1 rechts (Mitte→P3). */
    [[nodiscard]] static int curvatureSegmentAt (const ResponseCurve& curve, float normX) noexcept;

    /** Signierte Winkeländerung (Grad, [-180,180]) der Verbindungslinie
        zweier Finger zwischen Gesten-Start und jetzt -- Basis des Zwei-
        Finger-Dreh-Toggles. Positiv = gegen den Uhrzeigersinn (in
        Feld-Koordinaten, y nach oben). */
    [[nodiscard]] static float rotationDegrees (juce::Point<float> aStart, juce::Point<float> bStart,
                                                juce::Point<float> aNow, juce::Point<float> bNow) noexcept;

    /** Zwei-Finger-Drehung anwenden (Block C, Toggle 2→3 Punkte + Grundform):
        setzt {0,0},{0.5,0.5},{1,1} mit S-Kurve (clockwise) bzw. gespiegelter
        „?"-Kurve (counter-clockwise) als Segment-Krümmungen. Erneute Drehung
        wechselt nur die Grundform. Output-Range bleibt unangetastet. */
    static void applyRotationShape (ResponseCurve& curve, bool clockwise) noexcept;

    /** Mittelpunkt verschieben (Ein-Finger auf dem Griff ODER Zwei-Finger-
        Drag): normPos in Feld-Koordinaten, y wird über [outMin,outMax] in
        die normierte Punkt-Y-Koordinate zurückgerechnet. X geklemmt auf
        [kMidPointMinX, kMidPointMaxX], Y auf [0,1]. No-Op bei 2-Punkt-Kurve. */
    static void applyMidPointDrag (ResponseCurve& curve, juce::Point<float> normPos,
                                   float outMin, float outMax) noexcept;

    /** 3-Finger-2s-Reset (Block C): zurück auf 2-Punkt-Identität --
        Punkte {0,0},{1,1}, Krümmung 0, Output-Range [0,1]. */
    static void resetToDefault (ResponseCurve& curve) noexcept;

    // Grundform-Krümmung des Dreh-Toggles. TODO(design): Feinabstimmung.
    static constexpr float kShapeCurvature = 0.6f;
    static constexpr float kMidPointMinX   = 0.05f;
    static constexpr float kMidPointMaxX   = 0.95f;
};

} // namespace conduit::grid
