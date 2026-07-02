#include "LinkMenuPanel.h"

#include "Core/LinkClock.h"
#include "PushLookAndFeel.h"

namespace conduit
{

LinkMenuPanel::LinkMenuPanel (TransportSettings& settingsToUse, const LinkClock& linkClock)
    : settings (settingsToUse)
{
    const auto numPeers = (int) linkClock.getNumPeers();
    peersLabel.setText (numPeers == 0
                            ? juce::String ("Keine Link-Peers")
                            : juce::String (numPeers) + (numPeers == 1 ? " Link-Peer" : " Link-Peers"),
                        juce::dontSendNotification);
    peersLabel.setColour (juce::Label::textColourId,
                          numPeers > 0 ? push::colours::ledCyan : push::colours::textDim);

    syncToggle.setToggleState (settings.isStartStopSyncEnabled(), juce::dontSendNotification);
    syncToggle.setTooltip ("Play startet/stoppt die ganze Link-Session (inkl. Ableton)");
    syncToggle.onClick = [this]
    { settings.setStartStopSyncEnabled (syncToggle.getToggleState()); };

    offsetCaption.setText ("Clock-Offset", juce::dontSendNotification);
    offsetCaption.setColour (juce::Label::textColourId, push::colours::textDim);

    offsetSlider.setRange (-TransportSettings::maxClockOffsetMs,
                           TransportSettings::maxClockOffsetMs, 0.5);
    offsetSlider.setTextValueSuffix (" ms");
    offsetSlider.setDoubleClickReturnValue (true, 0.0);
    offsetSlider.setValue (settings.getClockOffsetMs(), juce::dontSendNotification);
    offsetSlider.onValueChange = [this]
    { settings.setClockOffsetMs (offsetSlider.getValue()); };

    tapCaption.setText ("Taps bis Tempo-Commit", juce::dontSendNotification);
    tapCaption.setColour (juce::Label::textColourId, push::colours::textDim);

    tapCountSlider.setRange (2.0, 8.0, 1.0);
    tapCountSlider.setValue (settings.getTapCount(), juce::dontSendNotification);
    tapCountSlider.setTooltip (juce::String::fromUTF8 ("n Taps erfassen das Tempo, der (n+1). Tap committet zur Session"));
    tapCountSlider.onValueChange = [this]
    { settings.setTapCount (juce::roundToInt (tapCountSlider.getValue())); };

    addAndMakeVisible (peersLabel);
    addAndMakeVisible (syncToggle);
    addAndMakeVisible (offsetCaption);
    addAndMakeVisible (offsetSlider);
    addAndMakeVisible (tapCaption);
    addAndMakeVisible (tapCountSlider);

    setSize (panelWidth, panelHeight);
}

void LinkMenuPanel::resized()
{
    auto bounds = getLocalBounds().reduced (12, 10);

    peersLabel.setBounds (bounds.removeFromTop (24));
    bounds.removeFromTop (4);
    syncToggle.setBounds (bounds.removeFromTop (28));
    bounds.removeFromTop (8);
    offsetCaption.setBounds (bounds.removeFromTop (18));
    offsetSlider.setBounds (bounds.removeFromTop (28));
    bounds.removeFromTop (8);
    tapCaption.setBounds (bounds.removeFromTop (18));
    tapCountSlider.setBounds (bounds.removeFromTop (28));
}

void LinkMenuPanel::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::panel);
}

} // namespace conduit
