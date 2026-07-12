#include "SettingsWindow.h"

#include "UI/AudioSettingsComponent.h"
#include "UI/CaptureSettingsComponent.h"
#include "UI/MidiRigSettingsComponent.h"
#include "UI/OscSettingsComponent.h"
#include "UI/UiSettingsComponent.h"

namespace conduit
{

namespace
{
    const juce::Colour tabBackground { 0xff23262b };

    //==========================================================================
    /** Metering-Tab: Clip-Reset-Modus (bindet MeterSettings). */
    class MeterSettingsTab final : public juce::Component
    {
    public:
        explicit MeterSettingsTab (MeterSettings& settingsToUse)
            : settings (settingsToUse)
        {
            header.setText ("Clip-Anzeige", juce::dontSendNotification);
            header.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
            addAndMakeVisible (header);

            label.setText (juce::String::fromUTF8 ("Zur\xc3\xbc" "cksetzen:"), juce::dontSendNotification);
            addAndMakeVisible (label);

            modeBox.addItem ("Manuell (Klick auf das Clip-Feld)", manualId);
            modeBox.addItem (juce::String::fromUTF8 ("Automatisch (~2,5 s, Klick zus\xc3\xa4tzlich)"),
                             automaticId);
            modeBox.setSelectedId (idFor (settings.getClipResetMode()), juce::dontSendNotification);
            modeBox.onChange = [this]
            {
                settings.setClipResetMode (modeBox.getSelectedId() == automaticId
                                               ? MeterSettings::ClipResetMode::automatic
                                               : MeterSettings::ClipResetMode::manual);
            };
            addAndMakeVisible (modeBox);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (18);
            header.setBounds (area.removeFromTop (28));
            area.removeFromTop (8);

            auto row = area.removeFromTop (28);
            label.setBounds (row.removeFromLeft (110));
            modeBox.setBounds (row);
        }

    private:
        static constexpr int manualId = 1, automaticId = 2;

        static int idFor (MeterSettings::ClipResetMode mode)
        {
            return mode == MeterSettings::ClipResetMode::automatic ? automaticId : manualId;
        }

        MeterSettings& settings;
        juce::Label header, label;
        juce::ComboBox modeBox;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterSettingsTab)
    };
}

//==============================================================================
SettingsWindow::SettingsWindow (juce::AudioDeviceManager* deviceManager, MeterSettings& meterSettings,
                                CaptureSettings& captureSettings, CaptureService& captureService,
                                OscSendSettings& oscSendSettings, OscController& oscController,
                                UiSettings& uiSettings,
                                MidiRigSettings& midiRigSettings, MidiPortHub& midiPortHub)
{
    setLookAndFeel (&darkLook);

    // Audio-Gerät nur im Standalone-Pfad (mit DeviceManager)
    if (deviceManager != nullptr)
        tabs.addTab (juce::String::fromUTF8 ("Audio-Ger\xc3\xa4t"), tabBackground,
                     new AudioSettingsComponent (*deviceManager), true);

    tabs.addTab ("Capture", tabBackground,
                 new CaptureSettingsComponent (captureSettings, captureService), true);
    tabs.addTab ("Metering", tabBackground, new MeterSettingsTab (meterSettings), true);
    tabs.addTab (juce::String::fromUTF8 ("Oberfl\xc3\xa4" "che"), tabBackground,
                 new UiSettingsComponent (uiSettings), true);
    tabs.addTab ("OSC", tabBackground,
                 new OscSettingsComponent (oscSendSettings, oscController), true);
    tabs.addTab ("MIDI", tabBackground,
                 new MidiRigSettingsComponent (midiRigSettings, midiPortHub), true);

    addAndMakeVisible (tabs);
    setSize (520, 520);
}

SettingsWindow::~SettingsWindow()
{
    setLookAndFeel (nullptr);
}

void SettingsWindow::paint (juce::Graphics& g)
{
    g.fillAll (tabBackground);
}

void SettingsWindow::resized()
{
    tabs.setBounds (getLocalBounds());
}

} // namespace conduit
