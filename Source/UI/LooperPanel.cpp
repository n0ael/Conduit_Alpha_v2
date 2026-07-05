#include "LooperPanel.h"

namespace conduit
{

namespace
{
    constexpr int headerHeight = 30;
    constexpr int stripHeight = 110;
    constexpr int controlsHeight = 58;
    constexpr int addTrackWidth = 28;
}

LooperPanel::LooperPanel (int number)
    : looperNumber (number)
{
    setName ("looperPanel" + juce::String (number));

    sourceCombo.setName ("looperSource" + juce::String (number));
    sourceCombo.onChange = [this]
    {
        const auto index = sourceCombo.getSelectedItemIndex();
        if (index >= 0 && index < (int) currentSources.size() && onSourceSelected != nullptr)
            onSourceSelected (currentSources[(size_t) index].key);
    };

    strip.onSegmentClicked = [this] (int bars)
    {
        if (onSegmentClicked != nullptr)
            onSegmentClicked (bars);
    };

    addTrackTile.onClick = [this]
    {
        if (onAddTrack != nullptr)
            onAddTrack();
    };

    addAndMakeVisible (sourceCombo);
    addAndMakeVisible (strip);
    addAndMakeVisible (controls);
    addAndMakeVisible (addTrackTile);

    setTrackCount (1);
}

void LooperPanel::wireTrack (LooperTrackStrip& track, int trackIndex)
{
    track.onGainChanged = [this, trackIndex] (float gain)
    { if (onTrackGain) onTrackGain (trackIndex, gain); };
    track.onPanChanged = [this, trackIndex] (float pan)
    { if (onTrackPan) onTrackPan (trackIndex, pan); };
    track.onMuteToggled = [this, trackIndex] (bool muted)
    { if (onTrackMute) onTrackMute (trackIndex, muted); };
    track.onSoloToggled = [this, trackIndex] (bool solo)
    { if (onTrackSolo) onTrackSolo (trackIndex, solo); };
    track.onStop = [this, trackIndex]
    { if (onTrackStop) onTrackStop (trackIndex); };
    track.onSlotTapped = [this, trackIndex] (int slotIndex)
    { if (onSlotTapped) onSlotTapped (trackIndex, slotIndex); };
    track.onHeaderLongPress = [this, trackIndex]
    { if (onTrackHeaderLongPress) onTrackHeaderLongPress (trackIndex); };
    track.onHeaderTapped = [this, trackIndex]
    { if (onTrackHeaderTapped) onTrackHeaderTapped (trackIndex); };
}

void LooperPanel::setTrackCount (int count)
{
    count = juce::jlimit (1, 4, count);
    if ((int) tracks.size() == count)
        return;

    while ((int) tracks.size() > count)
        tracks.pop_back();

    while ((int) tracks.size() < count)
    {
        const auto trackIndex = (int) tracks.size();
        auto track = std::make_unique<LooperTrackStrip> (trackIndex + 1);
        track->setVisibleSlots (visibleSlots);
        wireTrack (*track, trackIndex);
        addAndMakeVisible (*track);
        tracks.push_back (std::move (track));
    }

    addTrackTile.setVisible ((int) tracks.size() < 4);
    resized();
}

LooperTrackStrip& LooperPanel::getTrack (int trackIndex)
{
    jassert (trackIndex >= 0 && trackIndex < (int) tracks.size());
    return *tracks[(size_t) juce::jlimit (0, (int) tracks.size() - 1, trackIndex)];
}

void LooperPanel::setVisibleSlots (int count)
{
    visibleSlots = count;
    for (auto& track : tracks)
        track->setVisibleSlots (count);
}

void LooperPanel::setSources (std::vector<Source> sources, const juce::String& selectedKey)
{
    currentSources = std::move (sources);

    sourceCombo.clear (juce::dontSendNotification);

    int selectedId = 1;
    for (int i = 0; i < (int) currentSources.size(); ++i)
    {
        sourceCombo.addItem (currentSources[(size_t) i].label, i + 1);
        if (currentSources[(size_t) i].key == selectedKey)
            selectedId = i + 1;
    }

    if (! currentSources.empty())
        sourceCombo.setSelectedId (selectedId, juce::dontSendNotification);
}

void LooperPanel::setAudible (bool shouldGlow)
{
    if (audible != shouldGlow)
    {
        audible = shouldGlow;
        repaint (getLocalBounds().removeFromTop (headerHeight));
    }
}

void LooperPanel::setPulsePhase (float phase01)
{
    for (auto& track : tracks)
        track->setPulsePhase (phase01);
}

void LooperPanel::paint (juce::Graphics& g)
{
    g.setColour (push::colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    auto header = getLocalBounds().removeFromTop (headerHeight);
    g.setColour (push::colours::text);
    g.setFont (push::scaledFont (13.0f, true));
    g.drawText ("LOOPER " + juce::String (looperNumber),
                header.reduced (8, 0), juce::Justification::centredLeft, false);

    const auto led = juce::Rectangle<float> (9.0f, 9.0f)
                         .withCentre ({ (float) header.getRight() - 12.0f,
                                        (float) header.getCentreY() });
    g.setColour (audible ? push::colours::ledGreen : push::colours::outline);
    g.fillEllipse (led);
}

void LooperPanel::resized()
{
    auto bounds = getLocalBounds().reduced (4);

    auto header = bounds.removeFromTop (headerHeight - 4);
    header.removeFromLeft (76);   // "LOOPER n"
    header.removeFromRight (20);  // LED
    sourceCombo.setBounds (header.reduced (2));

    strip.setBounds (bounds.removeFromTop (stripHeight));
    bounds.removeFromTop (4);
    controls.setBounds (bounds.removeFromTop (controlsHeight));
    bounds.removeFromTop (4);

    auto trackArea = bounds;
    if (addTrackTile.isVisible())
        addTrackTile.setBounds (trackArea.removeFromRight (addTrackWidth)
                                    .withHeight (44).reduced (2));

    const auto count = (int) tracks.size();
    if (count > 0)
    {
        const auto trackWidth = trackArea.getWidth() / count;
        for (auto& track : tracks)
            track->setBounds (trackArea.removeFromLeft (trackWidth).reduced (2, 0));
    }
}

} // namespace conduit
