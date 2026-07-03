#include "LinkAudioStatusBadge.h"

#include "UI/PushLookAndFeel.h"

namespace conduit
{

LinkAudioStatusBadge::LinkAudioStatusBadge (GraphManager& graphManagerToUse,
                                            juce::String nodeUuidToShow)
    : graphManager (graphManagerToUse),
      nodeUuid (std::move (nodeUuidToShow))
{
    setInterceptsMouseClicks (false, false);  // reines Display
    startTimerHz (10);  // Statuswechsel sind selten — 10 Hz reichen
}

void LinkAudioStatusBadge::stopUpdates()
{
    stopTimer();
}

void LinkAudioStatusBadge::timerCallback()
{
    refreshNow();
}

void LinkAudioStatusBadge::refreshNow()
{
    // Transiente Auflösung pro Tick (5.3) — nullptr während Deleting/Swap
    auto* module = dynamic_cast<LinkAudioSendModule*> (graphManager.getModuleFor (nodeUuid));

    const auto status = module != nullptr ? module->getSendStatusForUi()
                                          : LinkAudioSendModule::SendStatus::offline;

    if (status != shownStatus)
    {
        shownStatus = status;
        repaint();
    }
}

void LinkAudioStatusBadge::paint (juce::Graphics& g)
{
    using SendStatus = LinkAudioSendModule::SendStatus;

    const auto colour = shownStatus == SendStatus::streaming  ? juce::Colour (0xff58d68d)
                      : shownStatus == SendStatus::announced  ? juce::Colour (0xffe8b339)
                                                              : juce::Colour (0xff5a6170);

    const auto text = shownStatus == SendStatus::streaming ? "streaming"
                    : shownStatus == SendStatus::announced ? "announced"
                                                           : "offline";

    auto bounds = getLocalBounds();
    const auto led = bounds.removeFromLeft (bounds.getHeight()).toFloat();

    g.setColour (colour);
    g.fillEllipse (led.withSizeKeepingCentre (10.0f, 10.0f));

    // Kein "Link:"-Präfix — die Kachel heißt ohnehin nach dem Modul, und der
    // schmale Slider-Streifen (168er-Kachel) schneidet längere Texte ab
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (push::scaledFont (13.0f));
    g.drawText (text, bounds, juce::Justification::centredLeft);
}

} // namespace conduit
