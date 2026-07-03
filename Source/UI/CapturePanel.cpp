#include "CapturePanel.h"

#include <algorithm>

#include "UI/PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr int rowHeight = 44;   // Touch-Target (CLAUDE.md 10)
    constexpr int tapsHeaderHeight = 18;
    constexpr int meterSteps = 64;  // dB-Quantisierung (Repaint-Schwelle)
    constexpr float meterFloorDb = -80.0f;

    const juce::Colour panelBackground { 0xff2a2e34 };
    const juce::Colour idleColour      { 0xff4a4f57 };
    const juce::Colour recordingColour { 0xffe6453c };
    const juce::Colour heldColour      { 0xffe6a23c };
    const juce::Colour floorColour     { 0xff4f8fc4 };

    juce::Colour colourForState (CaptureChannel::State state)
    {
        switch (state)
        {
            case CaptureChannel::State::recording:
            case CaptureChannel::State::awaitingSegment: return recordingColour;
            case CaptureChannel::State::held:            return heldColour;
            case CaptureChannel::State::idle:            break;
        }
        return idleColour;
    }

    /** Linearer Pegel → quantisierte Position auf der [-80..0]-dB-Skala. */
    int levelToSteps (float linearLevel)
    {
        const auto db = juce::Decibels::gainToDecibels (linearLevel, meterFloorDb);
        return juce::jlimit (0, meterSteps,
                             juce::roundToInt (juce::jmap (db, meterFloorDb, 0.0f,
                                                           0.0f, float (meterSteps))));
    }
}

//==============================================================================
void CapturePanel::ChannelRow::NameLabel::mouseDown (const juce::MouseEvent& event)
{
    juce::Label::mouseDown (event);

    if (isEditable())
        startTimer (longPressMs);
}

void CapturePanel::ChannelRow::NameLabel::mouseDrag (const juce::MouseEvent& event)
{
    juce::Label::mouseDrag (event);

    // Drag (Scroll-Geste im Viewport) ist kein Long-Press
    if (event.getDistanceFromDragStart() > 8)
        stopTimer();
}

void CapturePanel::ChannelRow::NameLabel::mouseUp (const juce::MouseEvent& event)
{
    stopTimer();
    juce::Label::mouseUp (event);
}

void CapturePanel::ChannelRow::NameLabel::timerCallback()
{
    stopTimer();
    showEditor();
}

//==============================================================================
CapturePanel::ChannelRow::ChannelRow (int captureIndexToUse, juce::String nameToUse,
                                      std::function<void (int, juce::String)> onCaptureToUse,
                                      std::function<void (juce::String)> onRenameToUse)
    : captureIndex (captureIndexToUse), name (std::move (nameToUse))
{
    captureButton.onClick = [this, onCapture = std::move (onCaptureToUse)]
    { onCapture (captureIndex, name); };
    captureButton.setEnabled (captureIndex >= 0);  // Tap ohne Puffer: nichts zu exportieren
    addAndMakeVisible (captureButton);

    nameLabel.setText (name, juce::dontSendNotification);
    nameLabel.setFont (juce::FontOptions { 13.0f });
    nameLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    nameLabel.setBorderSize ({ 0, 0, 0, 0 });

    if (onRenameToUse)
    {
        nameLabel.setEditable (false, true, false);  // Doppelklick; Long-Press via NameLabel
        nameLabel.onTextChange = [this, onRename = std::move (onRenameToUse)]
        { onRename (nameLabel.getText()); };  // leer → ChannelNames setzt auf Default zurück
    }

    addAndMakeVisible (nameLabel);
}

void CapturePanel::ChannelRow::setDisplayState (const DisplayState& newState)
{
    if (display == newState)
        return;

    display = newState;
    repaint();
}

void CapturePanel::ChannelRow::resized()
{
    captureButton.setBounds (getLocalBounds().removeFromRight (rowHeight));  // 44×44

    // Geometrie deckungsgleich mit paint(): LED links, dann der Name
    auto bounds = getLocalBounds();
    bounds.removeFromRight (rowHeight + 4);
    bounds.removeFromLeft (rowHeight / 2);
    nameLabel.setBounds (bounds.removeFromLeft (juce::jmin (96, bounds.getWidth() / 2)));
}

void CapturePanel::ChannelRow::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    bounds.removeFromRight (rowHeight + 4);  // Capture-Button + Lücke

    // Status-LED
    const auto led = bounds.removeFromLeft (rowHeight / 2).toFloat()
                         .withSizeKeepingCentre (12.0f, 12.0f);
    g.setColour (colourForState (display.state));
    g.fillEllipse (led);

    // Kanal-Name zeichnet das nameLabel (Inline-Editor) — hier nur den
    // Platz aussparen, Geometrie deckungsgleich mit resized()
    bounds.removeFromLeft (juce::jmin (96, bounds.getWidth() / 2));

    // Mini-Pegel: RMS-Füllung, Peak-Strich, Noise-Floor-Marker
    const auto meter = bounds.reduced (2, 14);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (meter.toFloat(), 3.0f);

    const auto xForSteps = [&meter] (int steps)
    { return meter.getX() + meter.getWidth() * steps / meterSteps; };

    if (display.rmsSteps > 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillRect (meter.withRight (xForSteps (display.rmsSteps)));
    }

    if (display.peakSteps > 0)
    {
        g.setColour (juce::Colours::white);
        g.fillRect (xForSteps (display.peakSteps) - 1, meter.getY(), 2, meter.getHeight());
    }

    // Noise-Floor-Marker: blauer Strich über die volle Zeilen-Pegelhöhe
    g.setColour (floorColour);
    g.fillRect (xForSteps (display.floorSteps) - 1, meter.getY() - 3, 2,
                meter.getHeight() + 6);
}

//==============================================================================
CapturePanel::CapturePanel (CaptureService& serviceToUse, ChannelNames& channelNamesToUse)
    : service (serviceToUse), channelNames (channelNamesToUse)
{
    channelViewport.setViewedComponent (&channelContainer, false);
    channelViewport.setScrollBarsShown (true, false);

    tapsHeaderLabel.setColour (juce::Label::textColourId,
                               juce::Colours::white.withAlpha (0.45f));
    tapsHeaderLabel.setFont (juce::FontOptions { 12.0f });
    tapsHeaderLabel.setJustificationType (juce::Justification::bottomLeft);
    channelContainer.addChildComponent (tapsHeaderLabel);  // sichtbar nur mit Taps

    addAndMakeVisible (channelViewport);

    service.addChangeListener (this);       // Kanalzahl (prepare) + Tap-Register/Rename
    channelNames.addChangeListener (this);  // Label-Änderungen (auch externe)
    rebuildChannelRows();
}

CapturePanel::~CapturePanel()
{
    service.removeChangeListener (this);
    channelNames.removeChangeListener (this);
}

//==============================================================================
void CapturePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Service- (RAM/Kanalzahl/Taps) und ChannelNames-Broadcasts laufen hier
    // auf — die Zeilenliste neu abgleichen (Repaint nur bei echter Änderung)
    rebuildChannelRows();
}

//==============================================================================
std::vector<CapturePanel::RowSpec> CapturePanel::makeRowSpecs() const
{
    std::vector<RowSpec> specs;

    // Hardware-Zeilen: effektives ChannelNames-Label (userLabel → Device-
    // Name → "In N") — dieselbe Quelle wie Export-Dateinamen und Port-UI
    const auto numChannels = service.getRingNumChannels();
    for (int ch = 0; ch < numChannels; ++ch)
        specs.push_back ({ ch, channelNames.getLabel (ChannelNames::Direction::input, ch),
                           false });

    // Abschnitt "Taps": genutzte virtuelle Slots (registriert ODER mit
    // gehaltenem Material) — Reihenfolge = Slot-Reihenfolge, stabil
    for (int slot = 0; slot < CaptureService::MAX_VIRTUAL_CHANNELS; ++slot)
        if (const auto info = service.getVirtualChannelUiInfo (slot); info.inUse)
            specs.push_back ({ info.captureIndex, info.name, true });

    return specs;
}

void CapturePanel::rebuildChannelRows()
{
    auto specs = makeRowSpecs();
    if (specs == currentRowSpecs)
        return;

    currentRowSpecs = std::move (specs);
    channelRows.clear();
    channelRows.reserve (currentRowSpecs.size());

    for (const auto& spec : currentRowSpecs)
    {
        // Nur Hardware-Zeilen sind umbenennbar (captureIndex == Kanal-Index);
        // Tap-Namen sind moduleIds — Rename am Node-Titel
        auto onRename = spec.isTap
                      ? std::function<void (juce::String)> {}
                      : [this, channel = spec.captureIndex] (juce::String newLabel)
                        {
                            channelNames.setUserLabel (ChannelNames::Direction::input,
                                                       channel, newLabel);
                        };

        auto row = std::make_unique<ChannelRow> (spec.captureIndex, spec.name,
                                                 [this] (int captureIndex, juce::String rowName)
                                                 { captureSingleChannel (captureIndex, rowName); },
                                                 std::move (onRename));
        channelContainer.addAndMakeVisible (*row);
        channelRows.push_back (std::move (row));
    }

    layoutChannelRows();
    repaint (channelArea);  // "keine Eingänge"-Hinweis ein-/ausblenden
}

void CapturePanel::layoutChannelRows()
{
    const auto width = juce::jmax (0, channelViewport.getWidth()
                                          - channelViewport.getScrollBarThickness());

    // Vor der ersten Tap-Zeile sitzt die "Taps"-Überschrift
    const auto firstTap = std::find_if (currentRowSpecs.begin(), currentRowSpecs.end(),
                                        [] (const RowSpec& spec) { return spec.isTap; });
    const auto hasTaps = firstTap != currentRowSpecs.end();

    int y = 0;
    for (size_t i = 0; i < channelRows.size(); ++i)
    {
        if (hasTaps && currentRowSpecs[i].isTap
            && static_cast<int> (i) == static_cast<int> (firstTap - currentRowSpecs.begin()))
        {
            tapsHeaderLabel.setBounds (0, y, width, tapsHeaderHeight);
            y += tapsHeaderHeight;
        }

        channelRows[i]->setBounds (0, y, width, rowHeight);
        y += rowHeight + 2;
    }

    tapsHeaderLabel.setVisible (hasTaps);
    channelContainer.setSize (width, y);
}

void CapturePanel::captureSingleChannel (int captureIndex, const juce::String& rowName)
{
    const auto numTracks = captureIndex >= 0 ? service.exportChannel (captureIndex) : 0;
    if (numTracks == 0 && onToast)
        onToast (rowName + ": keine aktive Aufnahme");
}

//==============================================================================
void CapturePanel::refresh()
{
    rebuildChannelRows();  // defensiv — prepare()-Broadcast ist der Hauptpfad

    const auto& meter = service.getInputMeter();

    for (size_t i = 0; i < channelRows.size(); ++i)
    {
        const auto ch = currentRowSpecs[i].captureIndex;
        const auto* channel = service.getChannel (ch);
        if (channel == nullptr)
            continue;  // Tap ohne Puffer: Zeile bleibt idle/stumm

        ChannelRow::DisplayState state;
        state.state      = channel->getState();
        state.rmsSteps   = levelToSteps (meter.getRms (ch));
        state.peakSteps  = levelToSteps (meter.getPeak (ch));
        state.floorSteps = levelToSteps (meter.getNoiseFloor (ch));
        channelRows[i]->setDisplayState (state);  // repaintet nur bei Änderung
    }
}

//==============================================================================
void CapturePanel::paint (juce::Graphics& g)
{
    g.fillAll (panelBackground);

    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (channelArea.toFloat(), 4.0f);

    if (channelRows.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.setFont (push::scaledFont (13.0f));
        g.drawText (juce::String::fromUTF8 ("keine Eing\xc3\xa4nge"), channelArea,
                    juce::Justification::centred);
    }
}

void CapturePanel::resized()
{
    // Reines Aktions-Panel: die volle Fläche gehört den Kanal-Zeilen
    // (Einstellungen liegen jetzt im Einstellungen-Fenster, Capture-Tab)
    channelArea = getLocalBounds().reduced (8, 6);
    channelViewport.setBounds (channelArea.reduced (4));
    layoutChannelRows();
}

} // namespace conduit
