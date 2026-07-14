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
    /** Metering-Tab: Clip-Reset-Modus + Ballistik (bindet MeterSettings).
        Die Ballistik wirkt LIVE beim Ziehen (User-Feintuning 14.07.2026)
        — app-weit für alle Pegelanzeigen. */
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

            ballisticsHeader.setText ("Ballistik", juce::dontSendNotification);
            ballisticsHeader.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
            addAndMakeVisible (ballisticsHeader);

            initBallisticSlider (rmsWindowSlider, rmsWindowLabel,
                                 juce::String::fromUTF8 ("RMS-Tr\xc3\xa4gheit"), " ms",
                                 MeterSettings::minRmsWindowSeconds * 1000.0,
                                 MeterSettings::maxRmsWindowSeconds * 1000.0, 10.0,
                                 settings.getRmsWindowSeconds() * 1000.0f,
                                 [this] (double ms)
                                 { settings.setRmsWindowSeconds ((float) (ms / 1000.0)); });

            initBallisticSlider (peakReleaseSlider, peakReleaseLabel,
                                 "Peak-Release", " s",
                                 MeterSettings::minPeakReleaseSeconds,
                                 MeterSettings::maxPeakReleaseSeconds, 0.05,
                                 settings.getPeakReleaseSeconds(),
                                 [this] (double s)
                                 { settings.setPeakReleaseSeconds ((float) s); });

            initBallisticSlider (peakHoldSlider, peakHoldLabel,
                                 "Peak-Hold", " s",
                                 MeterSettings::minPeakHoldSeconds,
                                 MeterSettings::maxPeakHoldSeconds, 0.1,
                                 settings.getPeakHoldSeconds(),
                                 [this] (double s)
                                 { settings.setPeakHoldSeconds ((float) s); });
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (18);
            header.setBounds (area.removeFromTop (28));
            area.removeFromTop (8);

            auto row = area.removeFromTop (28);
            label.setBounds (row.removeFromLeft (110));
            modeBox.setBounds (row);

            area.removeFromTop (16);
            ballisticsHeader.setBounds (area.removeFromTop (28));
            area.removeFromTop (8);

            layoutRow (area, rmsWindowLabel, rmsWindowSlider);
            layoutRow (area, peakReleaseLabel, peakReleaseSlider);
            layoutRow (area, peakHoldLabel, peakHoldSlider);
        }

    private:
        static constexpr int manualId = 1, automaticId = 2;

        static int idFor (MeterSettings::ClipResetMode mode)
        {
            return mode == MeterSettings::ClipResetMode::automatic ? automaticId : manualId;
        }

        void initBallisticSlider (juce::Slider& slider, juce::Label& sliderLabel,
                                  const juce::String& text, const juce::String& suffix,
                                  double min, double max, double step, double value,
                                  std::function<void (double)> apply)
        {
            sliderLabel.setText (text, juce::dontSendNotification);
            addAndMakeVisible (sliderLabel);

            slider.setRange (min, max, step);
            slider.setTextValueSuffix (suffix);
            slider.setValue (value, juce::dontSendNotification);
            slider.onValueChange = [&slider, applyFn = std::move (apply)]
            { applyFn (slider.getValue()); };
            addAndMakeVisible (slider);
        }

        static void layoutRow (juce::Rectangle<int>& area, juce::Label& rowLabel,
                               juce::Component& control)
        {
            auto row = area.removeFromTop (30);
            rowLabel.setBounds (row.removeFromLeft (110));
            control.setBounds (row.reduced (0, 2));
            area.removeFromTop (6);
        }

        MeterSettings& settings;
        juce::Label header, label;
        juce::ComboBox modeBox;

        juce::Label ballisticsHeader;
        juce::Label rmsWindowLabel, peakReleaseLabel, peakHoldLabel;
        juce::Slider rmsWindowSlider   { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
        juce::Slider peakReleaseSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
        juce::Slider peakHoldSlider    { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterSettingsTab)
    };
}

//==============================================================================
SettingsWindow::SettingsWindow (juce::AudioDeviceManager* deviceManager, MeterSettings& meterSettings,
                                CaptureSettings& captureSettings, CaptureService& captureService,
                                OscSendSettings& oscSendSettings, OscController& oscController,
                                UiSettings& uiSettings,
                                MidiRigSettings& midiRigSettings, MidiPortHub& midiPortHub,
                                MidiProfileLibrary& midiProfileLibrary,
                                ControllerProfileLibrary& controllerProfileLibrary)
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
                 new MidiRigSettingsComponent (midiRigSettings, midiPortHub,
                                               midiProfileLibrary, controllerProfileLibrary), true);

    addAndMakeVisible (tabs);
    // M4: der MIDI-Tab hat jetzt zwei Profile-Sektionen (Klangerzeuger +
    // Controller) uebereinander -- etwas mehr Hoehe, damit die Geraeteliste
    // nicht auf einen Scroll-Streifen zusammenschrumpft.
    setSize (520, 640);
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
