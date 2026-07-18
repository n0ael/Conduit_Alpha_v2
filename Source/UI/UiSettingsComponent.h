#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/UiSettings.h"

namespace conduit
{

//==============================================================================
/**
    Oberflächen-Einstellungen als Formular — der „Oberfläche"-Tab des
    Einstellungen-Fensters UND der Inhalt des schwebenden Dev-Panels
    (dieselbe Komponente, derselbe Broadcaster → automatisch synchron).

      - UI-Größe:     50–200 % in 10er-Rastern (wie Ableton). Commit am
                      Drag-Ende — das Fenster skaliert unter dem Slider weg,
                      kontinuierliches Anwenden wäre eine Feedback-Schleife.
      - Schriftgröße: 80–140 % in 5er-Rastern, gleiches Commit-Muster.
      - UI-Framerate: Nativ (max 120) / 60 fps / 30 fps — globales Limit
                      der UiFramePacer-Refreshes (Meter, Scopes, Marker).
      - Dev-Modus:    Toggle — zeigt die DEV-Buttons in den Modul-Köpfen.
      - DSP-Meter:    Toggle — „DSP x % ⌀ / y % pk · N XRuns" im Transport,
                      unabhängig vom Dev-Modus (Default an).

    Schreibt ausschließlich in UiSettings [Message Thread]; die ANWENDUNG
    (setGlobalScaleFactor, push::setFontScale + LnF-Kaskade) übernimmt der
    EngineEditor als ChangeListener.

    Controls public — headless UI-Tests treiben sie direkt (Konvention).
*/
class UiSettingsComponent final : public juce::Component,
                                  private juce::ChangeListener
{
public:
    explicit UiSettingsComponent (UiSettings& settingsToUse);
    ~UiSettingsComponent() override;

    void resized() override;

    [[nodiscard]] static int preferredHeight() noexcept { return 422; }

    //==========================================================================
    // Controls public für headless Tests
    juce::Slider uiScaleSlider   { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Slider fontScaleSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::ComboBox fpsLimitCombo;   // Nativ (max 120) / 60 / 30
    // Interaktions-Zoom-Grenze der Node-Canvas (ADR 008 M3a, Dev-Tuning)
    juce::Slider minZoomSlider   { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    // Pinch-Schwelle: Spread-Änderung, ab der 2-Finger = Zoom (Dev-Tuning)
    juce::Slider pinchDeadZoneSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    // Zoom-Antwort: Gesamt-Geschwindigkeit + progressive Kurve (Dev-Tuning)
    juce::Slider zoomStrengthSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Slider zoomCurveSlider    { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    // Gesten-Glättung gegen Touch-Sensor-Rauschen (Dev-Tuning)
    juce::Slider smoothingSlider    { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    // Text im ctor via String::fromUTF8 (MSVC-CP1252-Falle bei Umlaut-Literalen)
    juce::ToggleButton devModeToggle;
    juce::ToggleButton dspMeterToggle;
    juce::ToggleButton softKeyboardToggle;

    /** Slider-Werte (Prozent) in die Settings committen — Tests rufen direkt. */
    void applyUiScale();
    void applyFontScale();

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    /** Settings → Controls (Start + jeder Settings-Broadcast). */
    void syncControls();

    void layoutRow (juce::Rectangle<int>& area, juce::Label& label,
                    juce::Component& control, int rowHeight = 30);

    UiSettings& settings;

    juce::Label header;
    juce::Label uiScaleLabel   { {}, juce::String::fromUTF8 ("UI-Größe") };
    juce::Label fontScaleLabel { {}, juce::String::fromUTF8 ("Schriftgröße") };
    juce::Label fpsLimitLabel  { {}, juce::String::fromUTF8 ("UI-Framerate") };
    // 110-px-Label-Spalte: Text bewusst kurz (nie quetschen, User-Regel 07/2026)
    juce::Label minZoomLabel   { {}, juce::String::fromUTF8 ("Interakt.-Zoom") };
    juce::Label pinchDeadZoneLabel { {}, juce::String::fromUTF8 ("Pinch-Schwelle") };
    juce::Label zoomStrengthLabel  { {}, juce::String::fromUTF8 ("Zoom-Stärke") };
    juce::Label zoomCurveLabel     { {}, juce::String::fromUTF8 ("Zoom-Kurve") };
    juce::Label smoothingLabel     { {}, juce::String::fromUTF8 ("Glättung") };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UiSettingsComponent)
};

} // namespace conduit
