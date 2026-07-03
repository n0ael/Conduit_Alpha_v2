#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Modules/ChassisSchema.h"

namespace conduit
{

//==============================================================================
/**
    Parameter-Setup-Popup des Dev-Modus (4.6) — lebt in einer CallOutBox
    über der Fader-Spalte: Bezier-Response-Kurve (zwei draggbare Kontroll-
    punkte, auf [0,1] begrenzt → Monotonie, ChassisSchema-Doku) PLUS die
    Min/Max-Felder des User-Regelbereichs (User-Wunsch 03.07.: Range gehört
    ins Kurven-Tool integriert).

    onCurveChanged feuert bei jedem Drag-Schritt mit dem Kurven-String
    ("x1 y1 x2 y2") bzw. einem LEEREN String für linear (Reset);
    onRangeChanged beim Committen der Min/Max-Felder — liefert der Aufrufer
    false (vom GraphManager abgelehnt), restauriert der Editor die Felder.
    Beide committen undo-fähig über den GraphManager.
*/
class CurveEditor final : public juce::Component
{
public:
    CurveEditor (const juce::String& initialCurve, double userMin, double userMax);

    std::function<void (const juce::String&)> onCurveChanged;
    std::function<bool (double newMin, double newMax)> onRangeChanged;

    static constexpr int preferredSize = 160;

    // Min/Max-Felder public — UI-Tests treiben sie direkt (Muster Panel)
    juce::Label minEdit, maxEdit;

    [[nodiscard]] ChassisSchema::BezierCurve getCurve() const noexcept { return curve; }

    /** Kontrollpunkt setzen (0 oder 1, Werte 0..1) — für Tests ohne Maus. */
    void setHandle (int handleIndex, float x, float y);

    /** Reset auf linear (entfernt die Kurve beim Aufrufer). */
    void resetToLinear();

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;

private:
    [[nodiscard]] juce::Rectangle<float> plotArea() const;
    [[nodiscard]] juce::Point<float> handlePosition (int handleIndex) const;
    void notifyChange();
    void commitRange();
    void refreshRangeFields();

    ChassisSchema::BezierCurve curve;
    bool isLinear = true;      // Anzeige-Zustand: keine Kurve gesetzt
    int draggedHandle = -1;
    double currentMin = 0.0, currentMax = 1.0;

    juce::TextButton resetButton { "linear" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveEditor)
};

} // namespace conduit
