#include "LooperPanel.h"

namespace conduit
{

namespace
{
    constexpr int headerHeight = 30;
    constexpr int stripHeight = 110;
    constexpr int controlsHeight = 58;
}

LooperPanel::LooperPanel (int number)
    : looperNumber (number)
{
    setName ("looperPanel" + juce::String (number));

    sourceCombo.setName ("looperSource" + juce::String (number));
    sourceCombo.onChange = [this]
    {
        // Über die Item-ID statt des Item-Index — die Separatoren der
        // Gruppen (lokal | Link, Peer | Peer) verschieben Indizes
        const auto index = sourceCombo.getSelectedId() - 1;
        applySelectedSourceColour();
        if (index >= 0 && index < (int) currentSources.size() && onSourceSelected != nullptr)
            onSourceSelected (currentSources[(size_t) index].key);
    };

    strip.onSegmentClicked = [this] (int bars)
    {
        if (onSegmentClicked != nullptr)
            onSegmentClicked (bars);
    };

    // FFT/WAVE pro Looper (07/2026, ersetzt die MST-Kachel — MST lebt
    // als globaler Toggle im MIXER-Tab): LED an = Spektrum
    viewTile.onClick = [this]
    {
        if (onViewToggled != nullptr)
            onViewToggled (! viewTile.isActive());
    };

    addAndMakeVisible (viewTile);
    addAndMakeVisible (sourceCombo);
    addAndMakeVisible (strip);
    addAndMakeVisible (controls);

    setTrackCount (1);
}

void LooperPanel::wireTrack (LooperTrackStrip& track, int trackIndex)
{
    track.onGainChanged = [this, trackIndex] (float gain)
    { if (onTrackGain) onTrackGain (trackIndex, gain); };
    track.onPanChanged = [this, trackIndex] (float pan)
    { if (onTrackPan) onTrackPan (trackIndex, pan); };
    track.onDistanceChanged = [this, trackIndex] (float distance)
    { if (onTrackDistance) onTrackDistance (trackIndex, distance); };
    track.onSendLevelChanged = [this, trackIndex] (int sendIndex, float level)
    { if (onTrackSendLevel) onTrackSendLevel (trackIndex, sendIndex, level); };
    track.onMuteToggled = [this, trackIndex] (bool muted)
    { if (onTrackMute) onTrackMute (trackIndex, muted); };
    track.onSoloToggled = [this, trackIndex] (bool solo)
    { if (onTrackSolo) onTrackSolo (trackIndex, solo); };
    track.onPlay = [this, trackIndex]
    { if (onTrackPlay) onTrackPlay (trackIndex); };
    track.onResetSync = [this, trackIndex]
    { if (onTrackResetSync) onTrackResetSync (trackIndex); };
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

    // Mixer-Layout folgt der Track-Zahl: Kompakt-XY ab 3 Tracks,
    // Send-Labels „S1…" nur bei genau 1 Track (Handoff 20.07.2026)
    for (auto& track : tracks)
    {
        track->setXyCompact ((int) tracks.size() >= 3);
        track->setSendLabelsVisible ((int) tracks.size() == 1);
    }

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

    // Direkt ins RootMenu: Separatoren (Gruppen-Trenner) und Einträge in
    // Quellfarbe kann die addItem-API der ComboBox nicht — die Auswahl
    // läuft weiterhin über Item-IDs (Index + 1), Ticks setzt showPopup
    auto* menu = sourceCombo.getRootMenu();

    // Keine Phantom-Vorauswahl (Looper-I/O): ist der gespeicherte Schlüssel
    // nicht (mehr) in der Liste — Legacy-Key, entfernter Slot —, bleibt die
    // Combo LEER statt fälschlich den ersten Eintrag anzuzeigen
    int selectedId = 0;
    for (int i = 0; i < (int) currentSources.size(); ++i)
    {
        const auto& source = currentSources[(size_t) i];

        if (source.separatorBefore && i > 0)
            menu->addSeparator();

        juce::PopupMenu::Item item (source.label);
        item.itemID = i + 1;
        if (! source.colour.isTransparent())
            item.colour = source.colour;
        menu->addItem (std::move (item));

        if (source.key == selectedKey)
            selectedId = i + 1;
    }

    sourceCombo.setSelectedId (selectedId, juce::dontSendNotification);

    applySelectedSourceColour();
}

void LooperPanel::applySelectedSourceColour()
{
    // Combo-Text in der Farbe der gewählten Quelle (wie Strip/Wellenform);
    // farblose Quellen (Master) fallen auf die Standard-Textfarbe zurück
    const auto index = sourceCombo.getSelectedId() - 1;
    const auto colour = index >= 0 && index < (int) currentSources.size()
                            ? currentSources[(size_t) index].colour
                            : juce::Colour();

    sourceCombo.setColour (juce::ComboBox::textColourId,
                           colour.isTransparent() ? push::colours::text : colour);
}

void LooperPanel::setSpectrumView (bool spectrum)
{
    viewTile.setActive (spectrum);
    strip.setView (spectrum ? LooperWaveformStrip::View::spectrum
                            : LooperWaveformStrip::View::waveform);
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
    viewTile.setBounds (header.removeFromRight (44).reduced (2));
    sourceCombo.setBounds (header.reduced (2));

    strip.setBounds (bounds.removeFromTop (stripHeight));
    bounds.removeFromTop (4);
    controls.setBounds (bounds.removeFromTop (controlsHeight));
    bounds.removeFromTop (4);

    auto trackArea = bounds;
    const auto count = (int) tracks.size();
    if (count > 0)
    {
        const auto trackWidth = trackArea.getWidth() / count;
        for (auto& track : tracks)
            track->setBounds (trackArea.removeFromLeft (trackWidth).reduced (2, 0));
    }
}

} // namespace conduit
