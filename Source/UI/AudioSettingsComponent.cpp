#include "AudioSettingsComponent.h"

namespace conduit
{

//==============================================================================
AudioSettingsComponent::AudioSettingsComponent (juce::AudioDeviceManager& deviceManager)
    : selector (deviceManager,
                0, 32,     // min/max Input-Kanäle — großzügig für Multichannel (ES-6 u. a.)
                0, 32,     // min/max Output-Kanäle — ES-3 (8), ESX-8CV etc.
                false,     // keine MIDI-Input-Auswahl
                false,     // keine MIDI-Output-Auswahl
                false,     // Kanäle einzeln, nicht als Stereo-Paare
                false)     // "Advanced"-Sektion sichtbar (kein Aufklapp-Button)
{
    setLookAndFeel (&darkLook);

    headerLabel.setText ("Audio-Einstellungen", juce::dontSendNotification);
    headerLabel.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    headerLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
    headerLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (headerLabel);
    addAndMakeVisible (selector);

    setSize (460, 540);
}

AudioSettingsComponent::~AudioSettingsComponent()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void AudioSettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff24272c));  // Conduit-Panel-Hintergrund
}

void AudioSettingsComponent::resized()
{
    auto bounds = getLocalBounds().reduced (12);

    headerLabel.setBounds (bounds.removeFromTop (28));
    bounds.removeFromTop (8);
    selector.setBounds (bounds);
}

} // namespace conduit
