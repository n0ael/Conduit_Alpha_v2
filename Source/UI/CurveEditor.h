#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "DragCursorHider.h"
#include "Modules/ChassisSchema.h"

namespace conduit
{

//==============================================================================
/**
    Parameter-Setup-Popup des Dev-Modus (4.6) — lebt in einer CallOutBox
    über der Fader-Spalte. Zwei Tabs:

    FADER-Tab: Bezier-Response-Kurve des Faders im Fenster der HARD-Range —
    die Kurve läuft von (0, userMin) nach (1, userMax), die beiden ENDPUNKTE
    sind vertikal draggbar und setzen userMin/userMax direkt (User-Wunsch
    03.07.: Range-Punkte zum Anfassen); die Min/Max-Textfelder bleiben
    zusätzlich. Zwei Kontrollpunkte formen die Kurve (auf [0,1] begrenzt →
    Monotonie, ChassisSchema-Doku), Reset auf linear.

    LINK-Tab (aktiv, sobald eine Link-Quelle gewählt ist): Response-Kurve
    des Control-Links (normalisierte Quelle → Modulationsform, z.B. fürs
    Gain-Matching) — reine 0..1-Ansicht ohne Range-Endpunkte.

    Callbacks committen undo-fähig über den GraphManager:
      onCurveChanged      — Fader-Kurve ("x1 y1 x2 y2", leer = linear)
      onRangeChanged      — userMin/userMax; false = abgelehnt → restauriert
      onLinkChanged       — Link-Quelle (leer = lösen) + Amount −1..+1
      onLinkCurveChanged  — Link-Kurve (leer = linear)
*/
class CurveEditor final : public juce::Component
{
public:
    /** linkSources: dsp-Parameter-Ids DESSELBEN Moduls (ohne den eigenen). */
    CurveEditor (const juce::String& initialCurve, double userMin, double userMax,
                 double hardMinToUse, double hardMaxToUse,
                 const juce::StringArray& linkSources = {},
                 const juce::String& currentLinkSource = {},
                 double currentLinkAmount = 0.0,
                 const juce::String& initialLinkCurve = {});
    ~CurveEditor() override { cursorHider.end(); }

    std::function<void (const juce::String&)> onCurveChanged;
    std::function<bool (double newMin, double newMax)> onRangeChanged;
    std::function<void (const juce::String& source, double amount)> onLinkChanged;
    std::function<void (const juce::String&)> onLinkCurveChanged;

    static constexpr int preferredSize = 170;

    //==========================================================================
    enum class Tab { fader, link };

    void setActiveTab (Tab tab);
    [[nodiscard]] Tab getActiveTab() const noexcept { return activeTab; }

    /** Kurve des AKTIVEN Tabs. */
    [[nodiscard]] ChassisSchema::BezierCurve getCurve() const noexcept;

    /** Kontrollpunkt (0/1) des aktiven Tabs setzen — für Tests ohne Maus. */
    void setHandle (int handleIndex, float x, float y);

    /** Range-Endpunkt (Fader-Tab) auf einen WERT ziehen — für Tests ohne
        Maus; committet via onRangeChanged (abgelehnt = keine Änderung). */
    void dragEndpointToValue (bool maxEndpoint, double value);

    /** Link-Tab: Start-/Endwert der Response setzen (0..1) — fallende
        Responses (Ende < Start) drehen die Richtung direkt in der Kurve
        (User-Wunsch 07/2026). Committet via onLinkCurveChanged. */
    void setLinkEndpoint (bool endPoint, float y);

    /** Reset der Kurve des aktiven Tabs auf linear. */
    void resetToLinear();

    //==========================================================================
    // Controls public — UI-Tests treiben sie direkt (Muster Panel)
    juce::Label minEdit, maxEdit;
    juce::ComboBox linkSourceBox;
    juce::Slider linkAmountSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::TextButton faderTabButton { "Fader" };
    juce::TextButton linkTabButton { "Link" };

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;

private:
    struct CurveState
    {
        ChassisSchema::BezierCurve curve { 0.25f, 0.25f, 0.75f, 0.75f };
        bool isLinear = true;
    };

    [[nodiscard]] CurveState& activeCurve() noexcept;
    [[nodiscard]] const CurveState& activeCurve() const noexcept;

    [[nodiscard]] juce::Rectangle<float> plotArea() const;

    // Fader-Tab: Wert ↔ Bildschirm-y im HARD-Range-Fenster
    [[nodiscard]] float yForValue (double value) const;
    [[nodiscard]] double valueForY (float y) const;

    [[nodiscard]] juce::Point<float> handlePosition (int handleIndex) const;   // 0/1 Kontrolle, 2 min, 3 max
    void notifyCurveChange();
    void commitRange();
    void refreshRangeFields();
    void commitLink();
    void updateTabButtons();

    CurveState faderCurve, linkCurve;
    float linkStartY = 0.0f, linkEndY = 1.0f;   // Response-Endpunkte (Link-Tab)
    Tab activeTab = Tab::fader;
    int draggedHandle = -1;

    double currentMin = 0.0, currentMax = 1.0;
    const double hardMin, hardMax;
    juce::StringArray sources;

    juce::TextButton resetButton { "linear" };

    ui::DragCursorHider cursorHider;   // Cursor weg während des Punkt-Ziehens

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveEditor)
};

} // namespace conduit
