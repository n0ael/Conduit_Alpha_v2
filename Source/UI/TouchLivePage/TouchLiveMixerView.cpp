#include "TouchLiveMixerView.h"

#include <algorithm>

#include "UI/PushLookAndFeel.h"

namespace conduit
{

namespace
{
    // Live-Farbwelt der LED-Buttons (docs/TouchLive.md §5): gelber
    // Track-Activator (leuchtet wenn NICHT gemutet), blaues Solo, rotes Arm
    const juce::Colour ledYellow { 0xffffd60a };
    const juce::Colour ledBlue   { 0xff4da8ff };

    constexpr int panHeight     = 20;
    constexpr int sendHeight    = 16;
    constexpr int buttonRow     = 26;
    constexpr int nameFooter    = 20;
    constexpr int stripPadding  = 3;
    constexpr int maxSendRows   = 4;

    [[nodiscard]] juce::Colour colourFromLive (const juce::var& value)
    {
        return juce::Colour (0xff000000u | (juce::uint32) (juce::int64) value);
    }

    [[nodiscard]] float floatField (const juce::ValueTree& item, const char* field,
                                    float fallback)
    {
        return (float) (double) item.getProperty (field, (double) fallback);
    }
}

//==============================================================================
TouchLiveChannelStrip::TouchLiveChannelStrip (TouchLiveClient& clientToUse, juce::String keyToUse)
    : activatorTile ("1", ledYellow),
      soloTile ("S", ledBlue),
      armTile (juce::String::fromUTF8 ("\xE2\x97\x8F"), push::colours::ledRed),
      client (clientToUse),
      key (std::move (keyToUse))
{
    addAndMakeVisible (fader);
    fader.onUserValue = [this] (float value) { sendVolume (value); };

    pan.setRange (-1.0, 1.0, 0.0);
    pan.setValue (0.0, juce::dontSendNotification);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.onValueChange = [this] { sendPan ((float) pan.getValue()); };
    addAndMakeVisible (pan);

    activatorTile.setTooltip (juce::String::fromUTF8 ("Track an/aus (Mute) — gelb = spielt"));
    activatorTile.onClick = [this] { toggleMute(); };
    addChildComponent (activatorTile);

    soloTile.setTooltip ("Solo");
    soloTile.onClick = [this] { toggleSolo(); };
    addChildComponent (soloTile);

    armTile.setTooltip (juce::String::fromUTF8 ("Aufnahme scharfschalten"));
    armTile.onClick = [this] { toggleArm(); };
    addChildComponent (armTile);
}

//==============================================================================
void TouchLiveChannelStrip::updateStructure (const juce::ValueTree& tracksItem,
                                             const juce::ValueTree& mixerItem)
{
    if (tracksItem.isValid())
    {
        kind      = tracksItem.getProperty ("kind", kind).toString();
        name      = tracksItem.getProperty ("name").toString();
        liveIndex = (int) tracksItem.getProperty ("index", liveIndex);

        if (tracksItem.hasProperty ("color"))
            colour = colourFromLive (tracksItem.getProperty ("color"));
    }

    const auto regular = isRegularTrack();
    activatorTile.setText (juce::String (liveIndex + 1));
    activatorTile.setVisible (regular);
    soloTile.setVisible (regular);
    armTile.setVisible (regular);

    // Send-Anzahl dynamisch (nur reguläre Tracks — M1a kann Return-Sends
    // nicht setzen)
    auto sendCount = 0;

    if (regular)
        if (const auto* sends = mixerItem.getProperty ("sends").getArray())
            sendCount = juce::jmin (maxSendRows, sends->size());

    if ((int) sendSliders.size() != sendCount)
        rebuildSendSliders (sendCount);

    resized();
    repaint();
}

void TouchLiveChannelStrip::rebuildSendSliders (int sendCount)
{
    sendSliders.clear();

    for (int i = 0; i < sendCount; ++i)
    {
        auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                      juce::Slider::NoTextBox);
        slider->setRange (0.0, 1.0, 0.0);
        slider->setDoubleClickReturnValue (true, 0.0);

        auto* raw = slider.get();
        slider->onValueChange = [this, raw, i] { sendSend (i, (float) raw->getValue()); };

        addAndMakeVisible (*slider);
        sendSliders.push_back (std::move (slider));
    }
}

void TouchLiveChannelStrip::updateValues (const juce::ValueTree& mixerItem, bool animate)
{
    if (! mixerItem.isValid())
        return;

    const auto volume = floatField (mixerItem, "vol", fader.getDisplayedValue());

    if (animate)
        fader.setRemoteValue (volume);
    else
        fader.setValueSilent (volume);

    pan.setValue ((double) floatField (mixerItem, "pan", (float) pan.getValue()),
                  juce::dontSendNotification);

    if (const auto* sends = mixerItem.getProperty ("sends").getArray())
        for (int i = 0; i < juce::jmin ((int) sendSliders.size(), sends->size()); ++i)
            sendSliders[(size_t) i]->setValue ((double) sends->getReference (i),
                                               juce::dontSendNotification);

    muted  = (bool) mixerItem.getProperty ("mute", muted);
    soloed = (bool) mixerItem.getProperty ("solo", soloed);
    armed  = (bool) mixerItem.getProperty ("arm", armed);
    refreshButtonLeds();
}

void TouchLiveChannelStrip::refreshButtonLeds()
{
    activatorTile.setActive (! muted);   // Live-Konvention: gelb = spielt
    soloTile.setActive (soloed);
    armTile.setActive (armed);
}

//==============================================================================
void TouchLiveChannelStrip::sendVolume (float value)
{
    // Optimistisch ins Modell (§5.1): andere lokale Controller (AlphaTrack-Bridge)
    // sehen den Wert sofort, statt auf Lives unterdrücktes Echo zu warten.
    client.applyLocalMixerValue (key, "vol", value);

    if (kind == "master")
    {
        juce::OSCMessage message { juce::OSCAddressPattern ("/live/master/set/volume") };
        message.addFloat32 (value);
        client.sendTouchValue (message);
        return;
    }

    if (kind == "return")
    {
        juce::OSCMessage message { juce::OSCAddressPattern ("/live/return/set/volume") };
        message.addInt32 (liveIndex);
        message.addFloat32 (value);
        client.sendTouchValue (message);
        return;
    }

    juce::OSCMessage message { juce::OSCAddressPattern ("/live/track/set/volume") };
    message.addString (key);   // Stable-ID überlebt Track-Reorder
    message.addFloat32 (value);
    client.sendTouchValue (message);
}

void TouchLiveChannelStrip::sendPan (float value)
{
    client.applyLocalMixerValue (key, "pan", value);

    if (kind == "master")
    {
        juce::OSCMessage message { juce::OSCAddressPattern ("/live/master/set/panning") };
        message.addFloat32 (value);
        client.sendTouchValue (message);
        return;
    }

    if (kind == "return")
    {
        juce::OSCMessage message { juce::OSCAddressPattern ("/live/return/set/panning") };
        message.addInt32 (liveIndex);
        message.addFloat32 (value);
        client.sendTouchValue (message);
        return;
    }

    juce::OSCMessage message { juce::OSCAddressPattern ("/live/track/set/panning") };
    message.addString (key);
    message.addFloat32 (value);
    client.sendTouchValue (message);
}

void TouchLiveChannelStrip::sendSend (int sendIndex, float value)
{
    client.applyLocalMixerArrayElement (key, "sends", sendIndex, value);

    juce::OSCMessage message { juce::OSCAddressPattern ("/live/track/set/send") };
    message.addString (key);
    message.addInt32 (sendIndex);
    message.addFloat32 (value);
    client.sendTouchValue (message);
}

void TouchLiveChannelStrip::toggleMute()
{
    // Optimistisch (Feel-Regel 5.1: nie auf Live-Feedback warten)
    muted = ! muted;
    refreshButtonLeds();
    client.applyLocalMixerValue (key, "mute", muted);

    juce::OSCMessage message { juce::OSCAddressPattern ("/live/track/set/mute") };
    message.addString (key);
    message.addInt32 (muted ? 1 : 0);
    client.sendCommand (message);
}

void TouchLiveChannelStrip::toggleSolo()
{
    soloed = ! soloed;
    refreshButtonLeds();
    client.applyLocalMixerValue (key, "solo", soloed);

    juce::OSCMessage message { juce::OSCAddressPattern ("/live/track/set/solo") };
    message.addString (key);
    message.addInt32 (soloed ? 1 : 0);
    client.sendCommand (message);
}

void TouchLiveChannelStrip::toggleArm()
{
    armed = ! armed;
    refreshButtonLeds();
    client.applyLocalMixerValue (key, "arm", armed);

    juce::OSCMessage message { juce::OSCAddressPattern ("/live/track/set/arm") };
    message.addString (key);
    message.addInt32 (armed ? 1 : 0);
    client.sendCommand (message);
}

//==============================================================================
void TouchLiveChannelStrip::resized()
{
    auto area = getLocalBounds().reduced (stripPadding);

    area.removeFromBottom (nameFooter);

    if (isRegularTrack())
    {
        auto buttons = area.removeFromBottom (buttonRow);
        const auto third = buttons.getWidth() / 3;
        activatorTile.setBounds (buttons.removeFromLeft (third).reduced (1));
        soloTile.setBounds (buttons.removeFromLeft (third).reduced (1));
        armTile.setBounds (buttons.reduced (1));
    }

    pan.setBounds (area.removeFromTop (panHeight).reduced (2, 0));

    for (auto& slider : sendSliders)
        slider->setBounds (area.removeFromTop (sendHeight).reduced (2, 0));

    fader.setBounds (area.reduced (0, 2));
}

void TouchLiveChannelStrip::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (push::colours::tile);
    g.fillRoundedRectangle (area, 4.0f);

    // Fußzeile: Name auf Track-Farbe (Schrift nie stauchen — kürzen)
    auto footer = getLocalBounds().reduced (stripPadding).removeFromBottom (nameFooter).toFloat();
    g.setColour (colour);
    g.fillRoundedRectangle (footer, 3.0f);

    g.setColour (colour.getPerceivedBrightness() > 0.55f ? juce::Colours::black
                                                         : juce::Colours::white);
    g.setFont (push::scaledFont (11.0f));
    g.drawFittedText (name, footer.toNearestInt().reduced (3, 0),
                      juce::Justification::centred, 1, 1.0f);
}

//==============================================================================
//==============================================================================
TouchLiveMixerView::TouchLiveMixerView (TouchLiveClient& clientToUse, LiveSetModel& modelToUse,
                                        TouchLiveMeterBus& meterBusToUse,
                                        TouchLiveSettings& settingsToUse)
    : client (clientToUse),
      model (modelToUse),
      meterBus (meterBusToUse),
      settings (settingsToUse),
      modelState (model.getState())
{
    modelState.addListener (this);
    settings.addChangeListener (this);

    viewport.setViewedComponent (&stripRow, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (12);
    addAndMakeVisible (viewport);

    rebuildStrips();
    // Meter-Refresh: UiFramePacer (nativ per VBlank, global gedrosselt);
    // ohne sichtbare Page ist der Tick ein No-op (isShowing-Guard).
}

TouchLiveMixerView::~TouchLiveMixerView()
{
    settings.removeChangeListener (this);
    modelState.removeListener (this);
    cancelPendingUpdate();
}

//==============================================================================
juce::String TouchLiveMixerView::domainNameOf (const juce::ValueTree& tree)
{
    if (tree.hasType (touchlive::id::domain))
        return tree.getProperty (touchlive::id::domainName).toString();

    if (tree.hasType (touchlive::id::item))
        return tree.getParent().getProperty (touchlive::id::domainName).toString();

    return {};
}

void TouchLiveMixerView::valueTreePropertyChanged (juce::ValueTree& tree,
                                                   const juce::Identifier& property)
{
    juce::ignoreUnused (property);
    const auto domain = domainNameOf (tree);

    if (domain == "mixer" && tree.hasType (touchlive::id::item))
    {
        const auto itemKey = tree.getProperty (touchlive::id::itemKey).toString();

        if (auto* strip = findStrip (itemKey))
            strip->updateValues (tree, true);
        else if (masterStrip != nullptr && masterStrip->getKey() == itemKey)
            masterStrip->updateValues (tree, true);

        return;
    }

    if (domain == "tracks")
        triggerAsyncUpdate();   // Name/Farbe/Index → Struktur-Refresh (coalesced)
}

void TouchLiveMixerView::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    const auto domain = domainNameOf (parent);

    if (domain == "tracks" || domain == "mixer" || parent.hasType (touchlive::id::liveSet))
        triggerAsyncUpdate();
}

void TouchLiveMixerView::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    const auto domain = domainNameOf (parent);

    if (domain == "tracks" || domain == "mixer")
        triggerAsyncUpdate();
}

void TouchLiveMixerView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    layoutStrips();   // Kanalbreite geändert
}

void TouchLiveMixerView::handleAsyncUpdate()
{
    rebuildStrips();
}

void TouchLiveMixerView::flushPendingRebuild()
{
    handleUpdateNowIfNeeded();
}

void TouchLiveMixerView::refreshTick()
{
    if (! isShowing())
        return;   // Page nicht sichtbar → kein Meter-Aufwand

    refreshMetersNow();
}

void TouchLiveMixerView::refreshMetersNow()
{
    const auto frame = meterBus.getFrameCounter();
    const auto fresh = frame != lastMeterFrame;
    lastMeterFrame = frame;

    const auto feedStrip = [this, fresh] (TouchLiveChannelStrip& strip)
    {
        if (fresh)
        {
            const auto level = meterBus.getLevel (strip.getKey());
            strip.fader.setMeterLevels (level.left, level.right);
        }

        strip.fader.tickMeterDisplay();   // Ballistik läuft auch ohne Frame
    };

    for (const auto& strip : strips)
        feedStrip (*strip);

    if (masterStrip != nullptr)
        feedStrip (*masterStrip);
}

//==============================================================================
TouchLiveChannelStrip* TouchLiveMixerView::findStrip (const juce::String& stripKey) const
{
    for (const auto& strip : strips)
        if (strip->getKey() == stripKey)
            return strip.get();

    return nullptr;
}

void TouchLiveMixerView::rebuildStrips()
{
    auto tracksDomain = model.getDomain ("tracks");
    auto mixerDomain  = model.getDomain ("mixer");

    struct Entry
    {
        juce::String key;
        juce::String kind;
        int index = 0;
        juce::ValueTree tracksItem;
    };

    std::vector<Entry> regulars, returns;
    juce::ValueTree masterItem;

    for (int i = 0; i < tracksDomain.getNumChildren(); ++i)
    {
        const auto item = tracksDomain.getChild (i);
        Entry entry { item.getProperty (touchlive::id::itemKey).toString(),
                      item.getProperty ("kind").toString(),
                      (int) item.getProperty ("index", 0), item };

        if (entry.key.isEmpty())
            continue;

        if (entry.kind == "master")
            masterItem = item;
        else if (entry.kind == "return")
            returns.push_back (std::move (entry));
        else
            regulars.push_back (std::move (entry));
    }

    const auto byIndex = [] (const Entry& a, const Entry& b) { return a.index < b.index; };
    std::sort (regulars.begin(), regulars.end(), byIndex);
    std::sort (returns.begin(), returns.end(), byIndex);
    regulars.insert (regulars.end(), std::make_move_iterator (returns.begin()),
                     std::make_move_iterator (returns.end()));

    // Strips per Key wiederverwenden (kein Flackern, Drag-Zustand bleibt)
    std::vector<std::unique_ptr<TouchLiveChannelStrip>> fresh;
    fresh.reserve (regulars.size());

    for (const auto& entry : regulars)
    {
        std::unique_ptr<TouchLiveChannelStrip> strip;

        for (auto& existing : strips)
            if (existing != nullptr && existing->getKey() == entry.key)
                strip = std::move (existing);

        if (strip == nullptr)
            strip = std::make_unique<TouchLiveChannelStrip> (client, entry.key);

        const auto mixerItem = mixerDomain.getChildWithProperty (touchlive::id::itemKey,
                                                                 entry.key);
        strip->updateStructure (entry.tracksItem, mixerItem);
        strip->updateValues (mixerItem, false);
        stripRow.addAndMakeVisible (*strip);
        fresh.push_back (std::move (strip));
    }

    strips = std::move (fresh);

    if (masterItem.isValid())
    {
        const auto masterKey = masterItem.getProperty (touchlive::id::itemKey).toString();

        if (masterStrip == nullptr || masterStrip->getKey() != masterKey)
        {
            masterStrip = std::make_unique<TouchLiveChannelStrip> (client, masterKey);
            addAndMakeVisible (*masterStrip);
        }

        const auto mixerItem = mixerDomain.getChildWithProperty (touchlive::id::itemKey,
                                                                 masterKey);
        masterStrip->updateStructure (masterItem, mixerItem);
        masterStrip->updateValues (mixerItem, false);
    }
    else
    {
        masterStrip.reset();
    }

    layoutStrips();
}

//==============================================================================
void TouchLiveMixerView::resized()
{
    layoutStrips();
}

void TouchLiveMixerView::layoutStrips()
{
    auto area = getLocalBounds();
    const auto stripWidth = settings.getChannelWidth();

    if (masterStrip != nullptr)
        masterStrip->setBounds (area.removeFromRight (stripWidth).reduced (2, 0));

    viewport.setBounds (area);

    const auto contentHeight = juce::jmax (0, area.getHeight()
                                                  - viewport.getScrollBarThickness());
    stripRow.setSize (juce::jmax (1, (int) strips.size() * stripWidth), contentHeight);

    int x = 0;

    for (const auto& strip : strips)
    {
        strip->setBounds (x, 0, stripWidth, contentHeight);
        x += stripWidth;
    }
}

void TouchLiveMixerView::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::background);

    if (strips.empty() && masterStrip == nullptr)
    {
        g.setColour (push::colours::textDim);
        g.setFont (push::scaledFont (15.0f));
        g.drawText (juce::String::fromUTF8 ("Keine Live-Verbindung — LIVE einschalten "
                                            "und ConduitRemote in Ableton wählen"),
                    getLocalBounds(), juce::Justification::centred);
    }
}

} // namespace conduit
