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

    [[nodiscard]] static int preferredHeight() noexcept { return 206; }

    //==========================================================================
    // Controls public für headless Tests
    juce::Slider uiScaleSlider   { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Slider fontScaleSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UiSettingsComponent)
};

} // namespace conduit
