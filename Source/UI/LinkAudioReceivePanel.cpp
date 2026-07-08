#include "LinkAudioReceivePanel.h"

namespace conduit
{

LinkAudioReceivePanel::LinkAudioReceivePanel (juce::ValueTree nodeTreeToBind,
                                              GraphManager& graphManagerToUse)
    : nodeTree (nodeTreeToBind),
      graphManager (graphManagerToUse),
      nodeUuid (nodeTree.getProperty (id::nodeId).toString())
{
    nodeTree.addListener (this);

    channelButton.onClick = [this] { showChannelMenu(); };
    addAndMakeVisible (channelButton);

    latencySlider.setRange (LinkAudioReceiveModule::latencyMinMs,
                            LinkAudioReceiveModule::latencyMaxMs, 1.0);
    latencySlider.setTextValueSuffix (" ms");
    latencySlider.setValue ((double) latencyParam().getProperty (id::paramValue,
                                LinkAudioReceiveModule::latencyDefaultMs),
                            juce::dontSendNotification);
    latencySlider.onValueChange = [this]
    {
        // Parameter-Pflege ohne Undo (wie Send-Gains/OSC, 6.1) — der
        // GraphManager spiegelt paramValue generisch ins Modul-Atomic.
        if (! updatesStopped)
            if (auto param = latencyParam(); param.isValid())
                param.setProperty (id::paramValue, latencySlider.getValue(), nullptr);
    };
    addAndMakeVisible (latencySlider);

    refreshNow();
    startTimerHz (10);   // Status-Refresh (CLAUDE.md 10)
}

LinkAudioReceivePanel::~LinkAudioReceivePanel()
{
    stopUpdates();
}

void LinkAudioReceivePanel::stopUpdates()
{
    // Phase 1 (5.3): Rendering/Interaktion sofort stoppen, Listener lösen
    updatesStopped = true;
    stopTimer();
    nodeTree.removeListener (this);
    channelButton.setEnabled (false);
    latencySlider.setEnabled (false);
}

//==============================================================================
void LinkAudioReceivePanel::refreshNow()
{
    if (updatesStopped)
        return;

    const auto peer    = nodeTree.getProperty (id::targetPeer).toString();
    const auto channel = nodeTree.getProperty (id::targetChannel).toString();

    channelButton.setButtonText (channel.isNotEmpty()
                                     ? peer + " / " + channel
                                     : juce::String::fromUTF8 ("Kanal wählen…"));

    // Transiente Modul-Auflösung pro Tick — kein gehaltener Pointer (5.3)
    if (auto* module = dynamic_cast<LinkAudioReceiveModule*> (graphManager.getModuleFor (nodeUuid)))
    {
        status          = module->getReceiveStatusForUi();
        bufferedSeconds = module->getBufferedSecondsForUi();
    }
    else
    {
        status          = LinkAudioReceiveModule::ReceiveStatus::offline;
        bufferedSeconds = 0.0f;
    }

    repaint (statusRowBounds());
}

void LinkAudioReceivePanel::applyChannelChoice (const juce::String& peer, const juce::String& channel)
{
    if (updatesStopped)
        return;

    // Kanal-Wahl schreibt den Wunsch in den Subtree — der GraphManager
    // spiegelt zum Modul (rebind). Ohne Undo (Pflege-Semantik wie 6.1).
    nodeTree.setProperty (id::targetPeer,    peer,    nullptr);
    nodeTree.setProperty (id::targetChannel, channel, nullptr);
    refreshNow();
}

void LinkAudioReceivePanel::showChannelMenu()
{
    if (updatesStopped)
        return;

    auto* module = dynamic_cast<LinkAudioReceiveModule*> (graphManager.getModuleFor (nodeUuid));
    const auto channels = module != nullptr ? module->getAvailableChannelsForUi()
                                            : std::vector<LinkClock::ChannelInfo>{};

    juce::PopupMenu menu;
    menu.addItem (1, "Trennen",
                  nodeTree.getProperty (id::targetChannel).toString().isNotEmpty());
    menu.addSeparator();

    if (channels.empty())
        menu.addItem (2, juce::String::fromUTF8 ("— keine Kanäle in der Session —"), false);

    int itemId = 100;
    for (const auto& channel : channels)
        menu.addItem (itemId++, channel.peerName + " / " + channel.name);

    // Async (JUCE_MODAL_LOOPS_PERMITTED=0); SafePointer gegen Panel-Teardown
    // während das Menü offen ist (5.3).
    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (&channelButton),
        [safeThis = juce::Component::SafePointer<LinkAudioReceivePanel> (this), channels] (int result)
        {
            if (safeThis == nullptr)
                return;

            if (result == 1)
                safeThis->applyChannelChoice ({}, {});
            else if (result >= 100 && result - 100 < static_cast<int> (channels.size()))
            {
                const auto& chosen = channels[static_cast<size_t> (result - 100)];
                safeThis->applyChannelChoice (chosen.peerName, chosen.name);
            }
        });
}

//==============================================================================
juce::ValueTree LinkAudioReceivePanel::latencyParam() const
{
    return nodeTree.getChildWithName (id::parameters)
                   .getChildWithProperty (id::paramId,
                                          juce::String (LinkAudioReceiveModule::latencyParamId));
}

juce::Rectangle<int> LinkAudioReceivePanel::statusRowBounds() const
{
    return { 0, topPadding + 2 * rowHeight, getWidth(), rowHeight };
}

void LinkAudioReceivePanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop (topPadding);
    channelButton.setBounds (area.removeFromTop (rowHeight).reduced (2));
    latencySlider.setBounds (area.removeFromTop (rowHeight).reduced (2));
    // dritte Zeile: Status (paint)
}

void LinkAudioReceivePanel::paint (juce::Graphics& g)
{
    const auto row = statusRowBounds().reduced (6, 4);

    const auto colour = status == LinkAudioReceiveModule::ReceiveStatus::streaming
                          ? juce::Colour (0xff58d68d)
                      : status == LinkAudioReceiveModule::ReceiveStatus::waiting
                          ? juce::Colour (0xffe8b339)
                      : status == LinkAudioReceiveModule::ReceiveStatus::searching
                          ? juce::Colour (0xffe8833a)
                          : juce::Colour (0xff5a6170);

    const auto led = row.withWidth (row.getHeight()).reduced (5).toFloat();
    g.setColour (colour);
    g.fillEllipse (led);

    const auto text = status == LinkAudioReceiveModule::ReceiveStatus::streaming
                        ? juce::String::fromUTF8 ("Streamt · ")
                              + juce::String (juce::roundToInt (bufferedSeconds * 1000.0f)) + " ms gepuffert"
                    : status == LinkAudioReceiveModule::ReceiveStatus::waiting
                        ? juce::String ("Wartet auf Audio")
                    : status == LinkAudioReceiveModule::ReceiveStatus::searching
                        ? juce::String ("Sucht Kanal in der Session")
                        : juce::String ("Offline");

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (text, row.withTrimmedLeft (row.getHeight() + 4), juce::Justification::centredLeft,
                      1, 1.0f);   // minimumHorizontalScale = 1.0 (User-Regel: nie stauchen)
}

//==============================================================================
void LinkAudioReceivePanel::valueTreePropertyChanged (juce::ValueTree& tree,
                                                      const juce::Identifier& property)
{
    if (updatesStopped)
        return;

    // Externe Wunsch-Änderung (Undo, Preset-Load) → Button-Text folgt
    if (tree == nodeTree && (property == id::targetPeer || property == id::targetChannel))
    {
        refreshNow();
        return;
    }

    // Externe latency_ms-Änderung (OSC, Undo) → Slider folgt still
    if (property == id::paramValue && tree.hasType (id::parameter)
        && tree.getProperty (id::paramId).toString()
               == juce::String (LinkAudioReceiveModule::latencyParamId))
    {
        latencySlider.setValue ((double) tree.getProperty (id::paramValue),
                                juce::dontSendNotification);
    }
}

} // namespace conduit
