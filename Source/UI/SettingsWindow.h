#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Core/MeterSettings.h"

namespace conduit
{

class CaptureSettings;
class CaptureService;
class OscController;
class OscSendSettings;
class UiSettings;

//==============================================================================
/**
    Gebündeltes Einstellungen-Fenster (CLAUDE.md 10) — ein Einstiegspunkt für
    die App-Einstellungen, als Tabs:
      - „Audio-Gerät" (juce::AudioDeviceSelectorComponent via
        AudioSettingsComponent) — nur wenn ein AudioDeviceManager vorliegt
        (Standalone-Pfad).
      - „Capture" (CaptureSettingsComponent — Threshold/Hold/Pre-Roll/Ring/
        RAM-Limit/Bit-Tiefe/Export-Ordner). Die Capture-AKTIONen bleiben in
        der Toolbar/CapturePanel.
      - „Metering" (Clip-Reset-Modus, bindet MeterSettings).
      - „Oberfläche" (UiSettingsComponent — UI-Skalierung, Schriftgröße,
        Dev-Modus; bindet UiSettings).
      - „OSC" (OscSettingsComponent — Empfangs-Status + Send-Ziel, 7.3).

    Wird non-modal in einem juce::DialogWindow gezeigt (EngineEditor,
    launchAsync). Dark-Look via LookAndFeel_V4 (Midnight).
*/
class SettingsWindow final : public juce::Component
{
public:
    SettingsWindow (juce::AudioDeviceManager* deviceManager, MeterSettings& meterSettings,
                    CaptureSettings& captureSettings, CaptureService& captureService,
                    OscSendSettings& oscSendSettings, OscController& oscController,
                    UiSettings& uiSettings);
    ~SettingsWindow() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::LookAndFeel_V4 darkLook { juce::LookAndFeel_V4::getMidnightColourScheme() };
    juce::TabbedComponent tabs { juce::TabbedButtonBar::Orientation::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsWindow)
};

} // namespace conduit
