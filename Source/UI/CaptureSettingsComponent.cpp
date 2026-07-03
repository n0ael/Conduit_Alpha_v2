#include "CaptureSettingsComponent.h"

namespace conduit
{

namespace
{
    constexpr int labelWidth = 96;
    constexpr int gap        = 8;
}

//==============================================================================
CaptureSettingsComponent::CaptureSettingsComponent (CaptureSettings& settingsToUse,
                                                    CaptureService& serviceToUse)
    : settings (settingsToUse), service (serviceToUse)
{
    const auto styleLabel = [this] (juce::Label& label)
    {
        label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
    };

    for (auto* l : { &thresholdLabel, &holdLabel, &preRollLabel, &bufferLabel,
                     &ramLimitLabel, &bitDepthLabel })
        styleLabel (*l);

    // -- Schwelle / Hold: direkte Settings-Setter ------------------------------
    thresholdSlider.setRange (CaptureSettings::minThresholdDb,
                              CaptureSettings::maxThresholdDb, 1.0);
    thresholdSlider.setTextValueSuffix (" dB");
    thresholdSlider.onValueChange = [this]
    { settings.setThresholdDb (static_cast<float> (thresholdSlider.getValue())); };

    holdSlider.setRange (CaptureSettings::minHoldMinutes,
                         CaptureSettings::maxHoldMinutes, 1.0);
    holdSlider.setTextValueSuffix (" min");
    holdSlider.onValueChange = [this]
    { settings.setHoldMinutes (juce::roundToInt (holdSlider.getValue())); };

    autoCalibrateToggle.onClick = [this]
    { settings.setAutoCalibrate (autoCalibrateToggle.getToggleState()); };

    // -- Ring-Dimensionierung: Resize-Policy (Settings-Doku) -------------------
    preRollSlider.setRange (CaptureSettings::minPreRollSeconds,
                            CaptureSettings::maxPreRollSeconds, 5.0);
    preRollSlider.setTextValueSuffix (" s");
    preRollSlider.onDragEnd = [this] { applyRingSlider (preRollSlider, false); };
    preRollSlider.onValueChange = [this]
    {
        if (! preRollSlider.isMouseButtonDown())  // Textbox/Keyboard
            applyRingSlider (preRollSlider, false);
    };

    bufferSlider.setRange (CaptureSettings::minBufferMinutes,
                           CaptureSettings::maxBufferMinutes, 5.0);
    bufferSlider.setTextValueSuffix (" min");
    bufferSlider.onDragEnd = [this] { applyRingSlider (bufferSlider, true); };
    bufferSlider.onValueChange = [this]
    {
        if (! bufferSlider.isMouseButtonDown())
            applyRingSlider (bufferSlider, true);
    };

    // Bei aktiver Aufnahme bestätigt die UI async (13.2: keine Modal-Loops)
    settings.onPendingResize = [this] (const CaptureSettings::PendingResizeRequest& request)
    {
        syncControls();  // Slider zurück auf den aktiven Wert, solange offen

        const auto* fieldName =
            request.field == CaptureSettings::PendingResizeRequest::Field::bufferMinutes
                ? "Ring-Puffer" : "Pre-Roll";
        const auto message = juce::String (fieldName) + " auf "
                           + juce::String (request.requestedValue) + "? "
                           + juce::String::fromUTF8 ("Puffergr\xc3\xb6\xc3\x9f"
                                                     "e \xc3\xa4ndern l\xc3\xb6scht alle"
                                                     " aktuellen Aufnahmen. Fortfahren?");

        auto* settingsPtr = &settings;  // Settings überleben diese Component (Processor-Besitz)
        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon,
            juce::String::fromUTF8 ("Capture-Puffer \xc3\xa4ndern?"), message,
            "Fortfahren", "Abbrechen", this,
            juce::ModalCallbackFunction::create ([settingsPtr] (int result)
            {
                if (result == 1)
                    settingsPtr->confirmPendingResize();
                else
                    settingsPtr->cancelPendingResize();
            }));
    };

    // -- RAM-Limit -------------------------------------------------------------
    ramLimitSlider.setRange (CaptureSettings::minRamLimitGb,
                             CaptureSettings::maxRamLimitGb, 1.0);
    ramLimitSlider.setTextValueSuffix (" GB");
    ramLimitSlider.onValueChange = [this]
    { settings.setRamLimitGb (juce::roundToInt (ramLimitSlider.getValue())); };
    addAndMakeVisible (ramLimitSlider);

    // -- Export-Ziel -----------------------------------------------------------
    for (const auto bits : { 16, 24, 32 })
        bitDepthCombo.addItem (juce::String (bits) + " Bit", bits);
    bitDepthCombo.onChange = [this]
    { settings.setExportBitDepth (bitDepthCombo.getSelectedId()); };

    directoryHeader.setText (juce::String::fromUTF8 ("Export-Ordner"), juce::dontSendNotification);
    directoryHeader.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (directoryHeader);

    directoryButton.onClick = [this] { chooseExportDirectory(); };

    directoryLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    directoryLabel.setJustificationType (juce::Justification::centredLeft);

    releaseAfterExportToggle.onClick = [this]
    { settings.setReleaseAfterExport (releaseAfterExportToggle.getToggleState()); };

    ramWarningLabel.setText ("RAM-Limit erreicht!", juce::dontSendNotification);
    ramWarningLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    ramWarningLabel.setVisible (false);

    addAndMakeVisible (thresholdSlider);
    addAndMakeVisible (holdSlider);
    addAndMakeVisible (preRollSlider);
    addAndMakeVisible (bufferSlider);
    addAndMakeVisible (autoCalibrateToggle);
    addAndMakeVisible (releaseAfterExportToggle);
    addAndMakeVisible (bitDepthCombo);
    addAndMakeVisible (directoryButton);
    addAndMakeVisible (directoryLabel);
    addAndMakeVisible (ramWarningLabel);

    settings.addChangeListener (this);
    service.addChangeListener (this);   // RAM-Warnung
    syncControls();
}

CaptureSettingsComponent::~CaptureSettingsComponent()
{
    settings.onPendingResize = nullptr;
    settings.removeChangeListener (this);
    service.removeChangeListener (this);
}

//==============================================================================
void CaptureSettingsComponent::applyRingSlider (juce::Slider& slider, bool isBufferMinutes)
{
    const auto value = juce::roundToInt (slider.getValue());
    const auto outcome = isBufferMinutes ? settings.setBufferMinutes (value)
                                         : settings.setPreRollSeconds (value);
    juce::ignoreUnused (outcome);  // pendingConfirmation → Dialog läuft, syncControls resettet
}

void CaptureSettingsComponent::chooseExportDirectory()
{
    directoryChooser = std::make_unique<juce::FileChooser> (
        juce::String::fromUTF8 ("Export-Ordner w\xc3\xa4hlen"), settings.getExportDirectory());

    directoryChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& chooser)
        {
            if (chooser.getResult() != juce::File())
                settings.setExportDirectory (chooser.getResult());
        });
}

void CaptureSettingsComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    syncControls();
}

void CaptureSettingsComponent::syncControls()
{
    thresholdSlider.setValue (settings.getThresholdDb(), juce::dontSendNotification);
    holdSlider.setValue (settings.getHoldMinutes(), juce::dontSendNotification);
    preRollSlider.setValue (settings.getPreRollSeconds(), juce::dontSendNotification);
    bufferSlider.setValue (settings.getBufferMinutes(), juce::dontSendNotification);
    ramLimitSlider.setValue (settings.getRamLimitGb(), juce::dontSendNotification);
    autoCalibrateToggle.setToggleState (settings.getAutoCalibrate(), juce::dontSendNotification);
    releaseAfterExportToggle.setToggleState (settings.getReleaseAfterExport(),
                                             juce::dontSendNotification);
    bitDepthCombo.setSelectedId (settings.getExportBitDepth(), juce::dontSendNotification);
    directoryLabel.setText (settings.getExportDirectory().getFullPathName(),
                            juce::dontSendNotification);
    ramWarningLabel.setVisible (service.isRamWarningActive());
}

//==============================================================================
void CaptureSettingsComponent::layoutRow (juce::Rectangle<int>& area, juce::Label& label,
                                          juce::Component& control, int rowHeight)
{
    auto row = area.removeFromTop (rowHeight);
    label.setBounds (row.removeFromLeft (labelWidth));
    row.removeFromLeft (gap);
    control.setBounds (row);
    area.removeFromTop (gap);
}

void CaptureSettingsComponent::resized()
{
    auto area = getLocalBounds().reduced (18);

    layoutRow (area, thresholdLabel, thresholdSlider);
    layoutRow (area, holdLabel,      holdSlider);
    layoutRow (area, preRollLabel,   preRollSlider);
    layoutRow (area, bufferLabel,    bufferSlider);
    layoutRow (area, ramLimitLabel,  ramLimitSlider);
    layoutRow (area, bitDepthLabel,  bitDepthCombo);

    autoCalibrateToggle.setBounds (area.removeFromTop (28));
    area.removeFromTop (gap);
    releaseAfterExportToggle.setBounds (area.removeFromTop (28));
    area.removeFromTop (gap * 2);

    directoryHeader.setBounds (area.removeFromTop (22));
    auto dirRow = area.removeFromTop (30);
    directoryButton.setBounds (dirRow.removeFromLeft (110));
    dirRow.removeFromLeft (gap);
    directoryLabel.setBounds (dirRow);
    area.removeFromTop (gap);

    ramWarningLabel.setBounds (area.removeFromTop (22));
}

} // namespace conduit
