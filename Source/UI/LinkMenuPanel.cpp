#include "LinkMenuPanel.h"

#include "Core/LinkClock.h"
#include "PushLookAndFeel.h"

namespace conduit
{

LinkMenuPanel::LinkMenuPanel (TransportSettings& settingsToUse, const LinkClock& linkClock,
                              const juce::StringArray& metronomeTargets)
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

    metronomeCaption.setText ("Metronom-Ausgang", juce::dontSendNotification);
    metronomeCaption.setColour (juce::Label::textColourId, push::colours::textDim);

    for (int pair = 0; pair < metronomeTargets.size(); ++pair)
        metronomeTargetBox.addItem (metronomeTargets[pair], pair + 1);

    metronomeTargetBox.setSelectedId (
        juce::jlimit (0, juce::jmax (0, metronomeTargets.size() - 1),
                      settings.getMetronomeAnchor()) + 1,
        juce::dontSendNotification);
    metronomeTargetBox.onChange = [this]
    { settings.setMetronomeAnchor (metronomeTargetBox.getSelectedId() - 1); };

    const auto showMetronome = ! metronomeTargets.isEmpty();

    addAndMakeVisible (peersLabel);
    addAndMakeVisible (syncToggle);
    addAndMakeVisible (offsetCaption);
    addAndMakeVisible (offsetSlider);
    addAndMakeVisible (tapCaption);
    addAndMakeVisible (tapCountSlider);
    addChildComponent (metronomeCaption);
    addChildComponent (metronomeTargetBox);
    metronomeCaption.setVisible (showMetronome);
    metronomeTargetBox.setVisible (showMetronome);

    setSize (panelWidth, showMetronome ? panelHeight : panelHeight - 50);
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
    bounds.removeFromTop (8);
    metronomeCaption.setBounds (bounds.removeFromTop (18));
    metronomeTargetBox.setBounds (bounds.removeFromTop (26));
}

void LinkMenuPanel::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::panel);
}

} // namespace conduit
