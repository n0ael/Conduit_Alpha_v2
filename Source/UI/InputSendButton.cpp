#include "InputSendButton.h"

#include "UI/PushLookAndFeel.h"

namespace conduit
{

namespace
{
    // LED-Farben wie LinkAudioSendPanel (eine Optik für beide Send-Wege)
    [[nodiscard]] juce::Colour colourFor (LinkSendTaps::Status status)
    {
        using S = LinkSendTaps::Status;
        return status == S::streaming ? juce::Colour (0xff58d68d)
             : status == S::announced ? juce::Colour (0xffe8b339)
                                      : juce::Colour (0xff5a6170);
    }
}

InputSendButton::InputSendButton (ChannelNames& channelNamesToUse,
                                  const InputLinkSend* sendServiceToUse,
                                  int anchorPortToUse)
    : channelNames (channelNamesToUse),
      sendService (sendServiceToUse),
      anchorPort (anchorPortToUse)
{
    setTooltip (juce::String::fromUTF8 ("Link-Send an/aus (Kanal in der Link-Session)"));
    startTimerHz (10);  // Statuswechsel sind selten (Muster LinkAudioSendPanel)
}

InputSendButton::~InputSendButton() = default;

void InputSendButton::stopUpdates()
{
    stopTimer();
}

void InputSendButton::timerCallback()
{
    const auto current = sendService != nullptr ? sendService->statusForPort (anchorPort)
                                                : LinkSendTaps::Status::offline;
    if (current != status)
    {
        status = current;
        repaint();
    }
}

void InputSendButton::paint (juce::Graphics& g)
{
    const auto enabled = channelNames.isPortLinkSendEnabled (ChannelNames::Direction::input,
                                                             anchorPort);
    const auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    // Aktiv: LED-Farbe füllt den Knopf (streaming grün); inaktiv: dezente Kontur
    if (enabled)
    {
        g.setColour (colourFor (status).withAlpha (0.85f));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (juce::Colour (0xff15171a));
    }
    else
    {
        g.setColour (juce::Colour (0xff5a6170));
        g.drawRoundedRectangle (bounds, 4.0f, 1.2f);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
    }

    g.setFont (push::scaledFont (11.0f, true));
    g.drawText ("S", getLocalBounds(), juce::Justification::centred);
}

void InputSendButton::mouseUp (const juce::MouseEvent& event)
{
    if (! getLocalBounds().contains (event.getPosition()))
        return;

    // Nur das Flag schreiben — Engine (rebuildInputSends) und Port-UI
    // (rebuildPorts, ersetzt auch diesen Button) folgen dem Broadcast
    const auto enabled = channelNames.isPortLinkSendEnabled (ChannelNames::Direction::input,
                                                             anchorPort);
    channelNames.setPortLinkSendEnabled (ChannelNames::Direction::input, anchorPort, ! enabled);
}

} // namespace conduit
