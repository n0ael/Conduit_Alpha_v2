#pragma once

#include <functional>
#include <map>

#include <juce_graphics/juce_graphics.h>

namespace conduit
{

//==============================================================================
/**
    Zustandsmaschine der Canvas-Gesten-Leiter (ADR 008): Fingerzahl = Ebene.
    Reine Logik ohne Component-Abhängigkeit — headless testbar, Message
    Thread. Wiederverwendbar für spätere Canvas-Flächen.

    Leerraum-Regel (ADR 008): Der AUFRUFER füttert ausschließlich Touches,
    die auf dem Canvas-Hintergrund BEGINNEN — Touches auf Modulen gehören
    dem Modul (JUCE-Hit-Testing) und erreichen den Recognizer nie. Ein
    2-Finger-Pinch mit einem Finger auf einem Modul ist damit per
    Konstruktion KEIN Zoom (der Recognizer sieht nur einen Finger).

    Ebenen:
      1 Finger  — keine Recognizer-Aktion (Klick-Semantik des Canvas:
                  Kabel-Trennen, Doppel-Tap — bleibt beim Aufrufer)
      2 Finger  — kontinuierlich Pinch (Zoom) + Pan → onPinchPan
      3/4/5     — Hooks (M3a: Erkennung ja, Aktion no-op; M3b/M4 füllen
                  Birdeye, Seiten-Swipe, Seiten-Selektion)

    Übergänge: Jede Änderung der Fingerzahl beendet die laufende Ebene
    (onGestureEnd bei Ebene 2 → Persistenz-Write) und startet die neue mit
    frischer Referenz — Finger-dazu/-weg „springt" nie.
*/
class CanvasGestureRecognizer final
{
public:
    CanvasGestureRecognizer() = default;

    //==========================================================================
    // Callbacks — vom Canvas gesetzt. Alle optional (no-op wenn leer).

    /** Ebene 2, kontinuierlich: scaleFactor (relativ zum letzten Aufruf,
        1.0 = kein Zoom), panDelta (Screen-Pixel), anchor = aktueller
        Zentroid (Screen). */
    std::function<void (double scaleFactor, juce::Point<double> panDelta,
                        juce::Point<double> anchor)> onPinchPan;

    /** Ebene 2 endet (Finger weg oder Ebenen-Wechsel) — Persistenz-Anker. */
    std::function<void()> onGestureEnd;

    /** Ebene 3/4/5 beginnt (alle Finger unten, Referenz steht). M3a: no-op. */
    std::function<void (int fingerCount)> onLevelBegin;

    /** Ebene 3/4/5, kontinuierlich (M3b): geglättetes Zentroid-Delta der
        laufenden Geste — der 4-Finger-Swipe (Seiten-Peek) hängt hier. */
    std::function<void (int fingerCount, juce::Point<double> panDelta)> onLevelDrag;

    /** Ebene 3/4/5 endet (Fingerzahl ändert sich). Der Aufrufer entscheidet
        Commit/Snap-back über sein akkumuliertes Delta. */
    std::function<void (int fingerCount)> onLevelEnd;

    //==========================================================================
    void touchDown (int sourceIndex, juce::Point<float> position);
    void touchMove (int sourceIndex, juce::Point<float> position);
    void touchUp   (int sourceIndex);

    /** Alle Finger verwerfen (z. B. Component versteckt/Fokusverlust). */
    void reset();

    /** Dead-Zone der Zoom-Erkennung in Log-Einheiten (ln des Spread-
        Verhältnisses seit Gestenbeginn). Default 0.06 ≈ 6 % Spread-Änderung;
        der Canvas speist den Dev-Tuning-Wert (UiSettings::pinchDeadZone,
        User-Feedback 18.07.2026 — ungenaue Touchscreens brauchen mehr
        Toleranz). Soft-Bereich endet bei 3× der Schwelle; 0 = jede
        Spread-Änderung zoomt sofort. */
    void setZoomDeadZone (double logDeadZone) noexcept
    {
        zoomDeadZone = juce::jmax (0.0, logDeadZone);
    }

    [[nodiscard]] double getZoomDeadZone() const noexcept { return zoomDeadZone; }

    /** Progressive Zoom-Antwort (canvas_view::progressiveZoomResponse,
        User-Feedback 18.07.2026): gain < 1 senkt die Zoom-Geschwindigkeit,
        exponent > 1 lässt den Zoom langsam beginnen und kontinuierlich
        stärker werden. Defaults (1.0, 1.0) = neutral — die App speist die
        Dev-Tuning-Werte (UiSettings::zoomStrength/zoomCurve). */
    void setZoomResponse (double gain, double exponent) noexcept
    {
        zoomGain = juce::jmax (0.0, gain);
        zoomExponent = juce::jmax (1.0, exponent);
    }

    /** Gesten-Glättung (User-Feedback 18.07.2026, Release-Smoke): EMA-
        Tiefpass auf Zentroid + Spread gegen Touch-Sensor-Rauschen —
        verrauschte Fingerpositionen ließen die Karte beim Pannen zittern.
        0 = aus (roh, Default — die App speist UiSettings::gestureSmoothing);
        0.9 = sehr träge (~ mehr Latenz). */
    void setSmoothing (double amount) noexcept
    {
        smoothing = juce::jlimit (0.0, 0.95, amount);
    }

    [[nodiscard]] int getActiveFingerCount() const noexcept
    {
        return static_cast<int> (fingers.size());
    }

private:
    //==========================================================================
    struct PinchReference
    {
        juce::Point<double> centroid;
        double spread = 0.0;   // mittlerer Abstand zum Zentroid
    };

    [[nodiscard]] PinchReference currentReference() const;
    void levelChanged (int previousCount);

    std::map<int, juce::Point<double>> fingers;   // sourceIndex → Position
    PinchReference reference;                     // Ebene-2-Bezug (letzter Move)
    int activeLevel = 0;                          // gemeldete Ebene (2..5), 0 = keine

    // Weiche Zoom-Dead-Zone (canvas_view::softZoomResponse): der Zoom wird
    // gegen den GESTENSTART gerechnet und weich eingeblendet — beim Pannen
    // bleibt er exakt stehen (User-Feedback 18.07.2026)
    double gestureStartSpread = 0.0;
    double appliedLogZoom = 0.0;
    double zoomDeadZone = 0.06;   // Log-Einheiten, siehe setZoomDeadZone
    double zoomGain = 1.0;        // siehe setZoomResponse (neutral)
    double zoomExponent = 1.0;
    double smoothing = 0.0;       // siehe setSmoothing (0 = aus)
    PinchReference smoothedRef;   // EMA-Zustand der laufenden Ebene-2-Geste

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CanvasGestureRecognizer)
};

} // namespace conduit
