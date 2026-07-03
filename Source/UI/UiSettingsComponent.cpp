#include "UiSettingsComponent.h"

namespace conduit
{

UiSettingsComponent::UiSettingsComponent (UiSettings& settingsToUse)
    : settings (settingsToUse)
{
    header.setText (juce::String::fromUTF8 ("Oberfläche"), juce::dontSendNotification);
    header.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    addAndMakeVisible (header);

    // UI-Größe: 1%-Schritte wie Ableton; Commit erst am Drag-Ende — das
    // Fenster skaliert unter dem Slider weg (setGlobalScaleFactor trifft
    // ALLE Fenster), kontinuierliches Anwenden wäre eine Feedback-Schleife
    addAndMakeVisible (uiScaleLabel);
    uiScaleSlider.setRange (static_cast<double> (UiSettings::minUiScale) * 100.0,
                            static_cast<double> (UiSettings::maxUiScale) * 100.0, 1.0);
    uiScaleSlider.setTextValueSuffix (" %");
    uiScaleSlider.onDragEnd = [this] { applyUiScale(); };
    uiScaleSlider.onValueChange = [this]
    {
        if (! uiScaleSlider.isMouseButtonDown())   // Bahn-Klick/Pfeile/TextBox
            applyUiScale();
    };
    addAndMakeVisible (uiScaleSlider);

    addAndMakeVisible (fontScaleLabel);
    fontScaleSlider.setRange (static_cast<double> (UiSettings::minFontScale) * 100.0,
                              static_cast<double> (UiSettings::maxFontScale) * 100.0, 1.0);
    fontScaleSlider.setTextValueSuffix (" %");
    fontScaleSlider.onDragEnd = [this] { applyFontScale(); };
    fontScaleSlider.onValueChange = [this]
    {
        if (! fontScaleSlider.isMouseButtonDown())
            applyFontScale();
    };
    addAndMakeVisible (fontScaleSlider);

    devModeToggle.onClick = [this] { settings.setDevModeEnabled (devModeToggle.getToggleState()); };
    addAndMakeVisible (devModeToggle);

    settings.addChangeListener (this);
    syncControls();
}

UiSettingsComponent::~UiSettingsComponent()
{
    settings.removeChangeListener (this);
}

//==============================================================================
void UiSettingsComponent::applyUiScale()
{
    settings.setUiScale (static_cast<float> (uiScaleSlider.getValue() / 100.0));
}

void UiSettingsComponent::applyFontScale()
{
    settings.setFontScale (static_cast<float> (fontScaleSlider.getValue() / 100.0));
}

void UiSettingsComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    syncControls();   // Settings → Controls (auch vom Dev-Panel/Tab-Zwilling)
}

void UiSettingsComponent::syncControls()
{
    uiScaleSlider.setValue (static_cast<double> (settings.getUiScale()) * 100.0,
                            juce::dontSendNotification);
    fontScaleSlider.setValue (static_cast<double> (settings.getFontScale()) * 100.0,
                              juce::dontSendNotification);
    devModeToggle.setToggleState (settings.isDevModeEnabled(), juce::dontSendNotification);
}

//==============================================================================
void UiSettingsComponent::layoutRow (juce::Rectangle<int>& area, juce::Label& label,
                                     juce::Component& control, int rowHeight)
{
    auto row = area.removeFromTop (rowHeight);
    label.setBounds (row.removeFromLeft (110));
    control.setBounds (row.reduced (0, 2));
    area.removeFromTop (6);
}

void UiSettingsComponent::resized()
{
    auto area = getLocalBounds().reduced (18);

    header.setBounds (area.removeFromTop (28));
    area.removeFromTop (8);

    layoutRow (area, uiScaleLabel, uiScaleSlider);
    layoutRow (area, fontScaleLabel, fontScaleSlider);

    devModeToggle.setBounds (area.removeFromTop (30));
}

} // namespace conduit
