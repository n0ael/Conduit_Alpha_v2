#include "DevPanel.h"

#include "NumberFieldBracket.h"
#include "UI/PushLookAndFeel.h"
#include "UI/UiSettingsComponent.h"

namespace conduit
{

namespace
{
    // Inhalt des Dev-Panels: UiSettingsComponent (Oberflaeche) oben, darunter
    // die Grid-Dev-Werte Schwellbreite/Fade-Zeit (Block A4 -- Umzug aus
    // MpeShapingView, dort war es dieselbe Slider-Optik; hier NumberFieldBracket).
    class DevPanelContent final : public juce::Component
    {
    public:
        DevPanelContent (UiSettings& uiSettingsToUse, GridPanelSettings& gridPanelSettingsToUse)
            : uiSettingsComponent (uiSettingsToUse), gridPanelSettings (gridPanelSettingsToUse)
        {
            addAndMakeVisible (uiSettingsComponent);
            addAndMakeVisible (gridHeading);
            addAndMakeVisible (thresholdField);
            addAndMakeVisible (fadeField);
            addAndMakeVisible (tabMinWidthField);

            gridHeading.setColour (juce::Label::textColourId, push::colours::textDim);
            gridHeading.setFont (push::scaledFont (12.0f, true));

            thresholdField.setValue (gridPanelSettings.getEditorThresholdWidth(), juce::dontSendNotification);
            thresholdField.onValueChanged = [this] (double v)
            { gridPanelSettings.setEditorThresholdWidth ((int) v); };

            fadeField.setValue (gridPanelSettings.getNoteCircleFadeMs(), juce::dontSendNotification);
            fadeField.onValueChanged = [this] (double v)
            { gridPanelSettings.setNoteCircleFadeMs ((int) v); };

            // Block H3 Runde 3: Mindestbreite der Track-Tabs (der Strip
            // pollt den Wert live und wird ggf. horizontal scrollbar).
            tabMinWidthField.setValue (gridPanelSettings.getTrackTabMinWidthPx(),
                                       juce::dontSendNotification);
            tabMinWidthField.onValueChanged = [this] (double v)
            { gridPanelSettings.setTrackTabMinWidthPx ((int) v); };
        }

        void resized() override
        {
            auto area = getLocalBounds();
            uiSettingsComponent.setBounds (area.removeFromTop (UiSettingsComponent::preferredHeight()));

            gridHeading.setBounds (area.removeFromTop (24).reduced (8, 0));
            thresholdField.setBounds (area.removeFromTop (NumberFieldBracket::kRowHeight).reduced (8, 0));
            fadeField.setBounds (area.removeFromTop (NumberFieldBracket::kRowHeight).reduced (8, 0));
            tabMinWidthField.setBounds (area.removeFromTop (NumberFieldBracket::kRowHeight).reduced (8, 0));
        }

        [[nodiscard]] static int preferredHeight() noexcept
        {
            return UiSettingsComponent::preferredHeight() + 24 + NumberFieldBracket::kRowHeight * 3;
        }

    private:
        UiSettingsComponent uiSettingsComponent;
        GridPanelSettings& gridPanelSettings;
        juce::Label gridHeading { {}, "Grid" };
        NumberFieldBracket thresholdField { NumberFieldBracket::Config {
            (double) GridPanelSettings::minThresholdWidth, (double) GridPanelSettings::maxThresholdWidth,
            (double) GridPanelSettings::defaultThresholdWidth, 1.0, 0, 1.0, "Thr" } };
        NumberFieldBracket fadeField { NumberFieldBracket::Config {
            (double) GridPanelSettings::minNoteCircleFadeMs, (double) GridPanelSettings::maxNoteCircleFadeMs,
            (double) GridPanelSettings::defaultNoteCircleFadeMs, 1.0, 0, 1.0, "Fade" } };
        NumberFieldBracket tabMinWidthField { NumberFieldBracket::Config {
            (double) GridPanelSettings::minTrackTabMinWidthPx, (double) GridPanelSettings::maxTrackTabMinWidthPx,
            (double) GridPanelSettings::defaultTrackTabMinWidthPx, 1.0, 0, 1.0, "TabW" } };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DevPanelContent)
    };
} // namespace

DevPanel::DevPanel (UiSettings& uiSettingsToUse, GridPanelSettings& gridPanelSettingsToUse)
    : juce::DocumentWindow ("Dev", push::colours::panel,
                            juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setAlwaysOnTop (true);

    auto content = std::make_unique<DevPanelContent> (uiSettingsToUse, gridPanelSettingsToUse);
    content->setSize (380, DevPanelContent::preferredHeight());
    setContentOwned (content.release(), true);

    setResizable (false, false);
    setVisible (true);
}

void DevPanel::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();
}

} // namespace conduit
