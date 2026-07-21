#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "DragCursorHider.h"

namespace conduit
{

//==============================================================================
/**
    Schmales, vertikales Rand-Ribbon für globale Ausdruckswerte (Grid-Page,
    v2 — Pitch/Pressure/Slide): Antippen/Ziehen setzt den Wert aus der
    vertikalen Position (oben = 1, Maximum). Hält keinen eigenen
    Wertezustand außer dem Füllstand fürs Zeichnen — die Engine ist die
    Quelle der Wahrheit, der Besitzer verdrahtet onValueChanged.

    Fingertauglich (Breite ≥ 44px liegt beim Layout des Besitzers), keine
    Persistenz. Message Thread.

    bipolarMode: Füllindikator zeichnet von der vertikalen MITTE zum
    aktuellen Wert statt von unten (z. B. Pressure-Offset — Mitte = neutral,
    oben/unten = +/-). valueForNormY liefert unverändert [0, 1]; die
    Bipolar-Umrechnung (z. B. (value01 - 0.5) * 2) macht der Besitzer.
*/
class ExpressionRibbon final : public juce::Component
{
public:
    explicit ExpressionRibbon (juce::String labelText, bool bipolarMode = false);
    ~ExpressionRibbon() override { cursorHider.end(); }

    /** Feuert bei jedem Down/Drag mit dem aus der Y-Position abgeleiteten
        Wert [0, 1]. */
    std::function<void (float)> onValueChanged;

    /** Füllfarbe des Wertindikators pro Achse (Design-Mock Grid-Page v2:
        Pitch grün, Pressure orange, Slide cyan) — Default ledWhite. Track
        und Rahmen bleiben tile/outline. Andockstelle für die spätere
        konfigurierbare Achsen-Farbe. */
    void setFillColour (juce::Colour newColour);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;

    /** Reine Ableitung: normY (0 = oben) → value01 (1 = oben, Maximum).
        Testbar, keine UI-Abhängigkeit. */
    [[nodiscard]] static float valueForNormY (float normY) noexcept;

private:
    void handlePointer (const juce::MouseEvent& event);

    juce::String label;
    bool  bipolar = false;
    float currentValue = 0.0f;
    juce::Colour fillColour;   // Default push::colours::ledWhite (Ctor)

    ui::DragCursorHider cursorHider;   // Cursor weg während des Ziehens

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExpressionRibbon)
};

} // namespace conduit
