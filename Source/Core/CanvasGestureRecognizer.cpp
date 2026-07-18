#include "Core/CanvasGestureRecognizer.h"

#include <cmath>

#include "Core/CanvasViewport.h"

namespace conduit
{

//==============================================================================
CanvasGestureRecognizer::PinchReference CanvasGestureRecognizer::currentReference() const
{
    PinchReference ref;

    if (fingers.empty())
        return ref;

    for (const auto& [index, position] : fingers)
    {
        juce::ignoreUnused (index);
        ref.centroid += position;
    }

    const auto count = static_cast<double> (fingers.size());
    ref.centroid = { ref.centroid.x / count, ref.centroid.y / count };

    for (const auto& [index, position] : fingers)
    {
        juce::ignoreUnused (index);
        ref.spread += position.getDistanceFrom (ref.centroid);
    }

    ref.spread /= count;
    return ref;
}

void CanvasGestureRecognizer::levelChanged (int previousCount)
{
    // Laufende Ebene beenden
    if (activeLevel == 2 && onGestureEnd)
        onGestureEnd();
    else if (activeLevel >= 3 && onLevelEnd)
        onLevelEnd (activeLevel);

    juce::ignoreUnused (previousCount);

    // Neue Ebene mit frischer Referenz starten — Finger-dazu/-weg darf
    // nie einen Sprung in Zoom/Pan erzeugen
    const auto count = getActiveFingerCount();
    activeLevel = count >= 2 ? juce::jmin (count, 5) : 0;
    reference = currentReference();

    // Zoom-Bezug für die weiche Dead-Zone: der Spread am GESTENSTART
    gestureStartSpread = reference.spread;
    appliedLogZoom = 0.0;
    smoothedRef = reference;   // EMA-Zustand frisch aufsetzen (kein Nachziehen)

    if (activeLevel >= 3 && onLevelBegin)
        onLevelBegin (activeLevel);
}

//==============================================================================
void CanvasGestureRecognizer::touchDown (int sourceIndex, juce::Point<float> position)
{
    const auto previous = getActiveFingerCount();
    fingers[sourceIndex] = position.toDouble();
    levelChanged (previous);
}

void CanvasGestureRecognizer::touchMove (int sourceIndex, juce::Point<float> position)
{
    const auto it = fingers.find (sourceIndex);

    if (it == fingers.end())
        return;   // Finger begann nicht auf dem Canvas-Hintergrund

    it->second = position.toDouble();

    if (activeLevel != 2)
        return;   // Ebenen 3..5: M3a trackt nur, Aktion folgt in M3b/M4

    const auto raw = currentReference();

    // Gesten-Glättung (Release-Smoke 18.07.2026): EMA-Tiefpass gegen
    // Sensor-Rauschen des Touchscreens — sonst zittert die Karte beim
    // Pannen um den verrauschten Zentroid. smoothing 0 = Durchreichung.
    smoothedRef.centroid = raw.centroid * (1.0 - smoothing)
                         + smoothedRef.centroid * smoothing;
    smoothedRef.spread = raw.spread * (1.0 - smoothing)
                       + smoothedRef.spread * smoothing;
    const auto current = smoothedRef;

    // Weiche Zoom-Dead-Zone (User-Feedback 18.07.2026): die AKKUMULIERTE
    // Spread-Änderung seit Gestenbeginn läuft durch softZoomResponse —
    // Finger-Wackeln beim Pannen ändert den Zoom exakt gar nicht, echtes
    // Spreizen blendet weich ein. Gemeldet wird weiter inkrementell.
    // Spread <= 1 px (Finger aufeinander) → kein sinnvoller Zoom-Faktor.
    auto scaleFactor = 1.0;

    if (gestureStartSpread > 1.0 && current.spread > 1.0)
    {
        const auto rawLog = std::log (current.spread / gestureStartSpread);
        const auto gated  = canvas_view::softZoomResponse (rawLog, zoomDeadZone,
                                                           zoomDeadZone * 3.0);
        const auto effectiveLog = canvas_view::progressiveZoomResponse (gated, zoomGain,
                                                                        zoomExponent);
        scaleFactor = std::exp (effectiveLog - appliedLogZoom);
        appliedLogZoom = effectiveLog;
    }

    const auto panDelta = current.centroid - reference.centroid;
    reference = current;

    if (onPinchPan)
        onPinchPan (scaleFactor, panDelta, current.centroid);
}

void CanvasGestureRecognizer::touchUp (int sourceIndex)
{
    if (fingers.erase (sourceIndex) == 0)
        return;   // gehörte nicht zu einer Canvas-Geste

    levelChanged (getActiveFingerCount() + 1);
}

void CanvasGestureRecognizer::reset()
{
    if (! fingers.empty())
    {
        fingers.clear();
        levelChanged (0);
    }
}

} // namespace conduit
