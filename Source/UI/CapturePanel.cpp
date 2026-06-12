#include "CapturePanel.h"

#include <algorithm>

namespace conduit
{

namespace
{
    constexpr int rowHeight = 44;   // Touch-Target (CLAUDE.md 10)
    constexpr int tapsHeaderHeight = 18;
    constexpr int gap = 8;
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
CapturePanel::ChannelRow::ChannelRow (int captureIndexToUse, juce::String nameToUse,
                                      std::function<void (int, juce::String)> onCaptureToUse)
    : captureIndex (captureIndexToUse), name (std::move (nameToUse))
{
    captureButton.onClick = [this, onCapture = std::move (onCaptureToUse)]
    { onCapture (captureIndex, name); };
    captureButton.setEnabled (captureIndex >= 0);  // Tap ohne Puffer: nichts zu exportieren
    addAndMakeVisible (captureButton);
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

    // Kanal-Name (Hardware "inN" oder Tap-Spurname — drawText clippt)
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (juce::FontOptions { 13.0f });
    g.drawText (name, bounds.removeFromLeft (juce::jmin (96, bounds.getWidth() / 2)),
                juce::Justification::centredLeft);

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
CapturePanel::CapturePanel (CaptureSettings& settingsToUse, CaptureService& serviceToUse)
    : settings (settingsToUse), service (serviceToUse)
{
    // -- Schwelle / Hold: direkte Settings-Setter ------------------------------
    thresholdSlider.setRange (CaptureSettings::minThresholdDb,
                              CaptureSettings::maxThresholdDb, 1.0);
    thresholdSlider.setTextValueSuffix (" dB");
    thresholdSlider.onValueChange = [this]
    { settings.setThresholdDb (static_cast<float> (thresholdSlider.getValue())); };

    holdSlider.setRange (CaptureSettings::minHoldMinutes,
                         CaptureSettings::maxHoldMinutes, 1.0);
    holdSlider.setTextValueSuffix (" min Hold");
    holdSlider.onValueChange = [this]
    { settings.setHoldMinutes (juce::roundToInt (holdSlider.getValue())); };

    autoCalibrateToggle.onClick = [this]
    { settings.setAutoCalibrate (autoCalibrateToggle.getToggleState()); };

    // -- Ring-Dimensionierung: Resize-Policy (Settings-Doku) -------------------
    // Übernahme erst am Drag-Ende bzw. bei Textbox-Eingabe — sonst würde
    // jeder Drag-Tick eine Reallokation (oder den Bestätigungs-Dialog) treten
    preRollSlider.setRange (CaptureSettings::minPreRollSeconds,
                            CaptureSettings::maxPreRollSeconds, 5.0);
    preRollSlider.setTextValueSuffix (" s Pre-Roll");
    preRollSlider.onDragEnd = [this] { applyRingSlider (preRollSlider, false); };
    preRollSlider.onValueChange = [this]
    {
        if (! preRollSlider.isMouseButtonDown())  // Textbox/Keyboard
            applyRingSlider (preRollSlider, false);
    };

    bufferSlider.setRange (CaptureSettings::minBufferMinutes,
                           CaptureSettings::maxBufferMinutes, 5.0);
    bufferSlider.setTextValueSuffix (" min Ring");
    bufferSlider.onDragEnd = [this] { applyRingSlider (bufferSlider, true); };
    bufferSlider.onValueChange = [this]
    {
        if (! bufferSlider.isMouseButtonDown())
            applyRingSlider (bufferSlider, true);
    };

    // Bei aktiver Aufnahme bestätigt die UI async (13.2: keine Modal-Loops)
    settings.onPendingResize = [this] (const CaptureSettings::PendingResizeRequest& request)
    {
        syncControls();  // Slider zurück auf den aktiven Wert, solange offen

        const auto* fieldName =
            request.field == CaptureSettings::PendingResizeRequest::Field::bufferMinutes
                ? "Ring-Puffer" : "Pre-Roll";
        const auto message = juce::String (fieldName) + " auf "
                           + juce::String (request.requestedValue) + "? "
                           + juce::String::fromUTF8 ("Puffergr\xc3\xb6\xc3\x9f"
                                                     "e \xc3\xa4ndern l\xc3\xb6scht alle"
                                                     " aktuellen Aufnahmen. Fortfahren?");

        auto* settingsPtr = &settings;  // Settings überleben den Editor (Processor-Besitz)
        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon,
            juce::String::fromUTF8 ("Capture-Puffer \xc3\xa4ndern?"), message,
            "Fortfahren", "Abbrechen", this,
            juce::ModalCallbackFunction::create ([settingsPtr] (int result)
            {
                if (result == 1)
                    settingsPtr->confirmPendingResize();
                else
                    settingsPtr->cancelPendingResize();
            }));
    };

    // -- Export-Ziel -----------------------------------------------------------
    for (const auto bits : { 16, 24, 32 })
        bitDepthCombo.addItem (juce::String (bits) + " Bit", bits);
    bitDepthCombo.onChange = [this]
    { settings.setExportBitDepth (bitDepthCombo.getSelectedId()); };

    directoryButton.onClick = [this] { chooseExportDirectory(); };

    directoryLabel.setColour (juce::Label::textColourId,
                              juce::Colours::white.withAlpha (0.6f));
    directoryLabel.setJustificationType (juce::Justification::centredLeft);
    directoryLabel.setMinimumHorizontalScale (0.7f);

    releaseAfterExportToggle.onClick = [this]
    { settings.setReleaseAfterExport (releaseAfterExportToggle.getToggleState()); };

    ramWarningLabel.setText ("RAM-Limit!", juce::dontSendNotification);
    ramWarningLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    ramWarningLabel.setVisible (false);

    channelViewport.setViewedComponent (&channelContainer, false);
    channelViewport.setScrollBarsShown (true, false);

    tapsHeaderLabel.setColour (juce::Label::textColourId,
                               juce::Colours::white.withAlpha (0.45f));
    tapsHeaderLabel.setFont (juce::FontOptions { 12.0f });
    tapsHeaderLabel.setJustificationType (juce::Justification::bottomLeft);
    channelContainer.addChildComponent (tapsHeaderLabel);  // sichtbar nur mit Taps

    addAndMakeVisible (thresholdSlider);
    addAndMakeVisible (holdSlider);
    addAndMakeVisible (preRollSlider);
    addAndMakeVisible (bufferSlider);
    addAndMakeVisible (autoCalibrateToggle);
    addAndMakeVisible (releaseAfterExportToggle);
    addAndMakeVisible (bitDepthCombo);
    addAndMakeVisible (directoryButton);
    addAndMakeVisible (directoryLabel);
    addAndMakeVisible (ramWarningLabel);
    addAndMakeVisible (channelViewport);

    settings.addChangeListener (this);
    service.addChangeListener (this);   // RAM-Warnung + Kanalzahl (prepare)
    rebuildChannelRows();
    syncControls();
}

CapturePanel::~CapturePanel()
{
    settings.onPendingResize = nullptr;
    settings.removeChangeListener (this);
    service.removeChangeListener (this);
}

//==============================================================================
void CapturePanel::applyRingSlider (juce::Slider& slider, bool isBufferMinutes)
{
    const auto value = juce::roundToInt (slider.getValue());
    const auto outcome = isBufferMinutes ? settings.setBufferMinutes (value)
                                         : settings.setPreRollSeconds (value);

    // pendingConfirmation: onPendingResize hat den Dialog gestartet und
    // syncControls() den Slider zurückgesetzt — hier nichts weiter zu tun
    juce::ignoreUnused (outcome);
}

void CapturePanel::chooseExportDirectory()
{
    directoryChooser = std::make_unique<juce::FileChooser> (
        juce::String::fromUTF8 ("Export-Ordner w\xc3\xa4hlen"), settings.getExportDirectory());

    directoryChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& chooser)
        {
            if (chooser.getResult() != juce::File())
                settings.setExportDirectory (chooser.getResult());
        });
}

void CapturePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Settings-Broadcast, RAM-Warnung und prepare() (Device-/Kanalzahl-
    // Wechsel) laufen alle hier auf — ein kompletter Sync ist billig
    syncControls();
    rebuildChannelRows();
}

//==============================================================================
std::vector<CapturePanel::RowSpec> CapturePanel::makeRowSpecs() const
{
    std::vector<RowSpec> specs;

    const auto numChannels = service.getRingNumChannels();
    for (int ch = 0; ch < numChannels; ++ch)
        specs.push_back ({ ch, "in" + juce::String (ch + 1), false });

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
        auto row = std::make_unique<ChannelRow> (spec.captureIndex, spec.name,
                                                 [this] (int captureIndex, juce::String rowName)
                                                 { captureSingleChannel (captureIndex, rowName); });
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

void CapturePanel::syncControls()
{
    thresholdSlider.setValue (settings.getThresholdDb(), juce::dontSendNotification);
    holdSlider.setValue (settings.getHoldMinutes(), juce::dontSendNotification);
    preRollSlider.setValue (settings.getPreRollSeconds(), juce::dontSendNotification);
    bufferSlider.setValue (settings.getBufferMinutes(), juce::dontSendNotification);
    autoCalibrateToggle.setToggleState (settings.getAutoCalibrate(), juce::dontSendNotification);
    releaseAfterExportToggle.setToggleState (settings.getReleaseAfterExport(),
                                             juce::dontSendNotification);
    bitDepthCombo.setSelectedId (settings.getExportBitDepth(), juce::dontSendNotification);
    directoryLabel.setText (settings.getExportDirectory().getFullPathName(),
                            juce::dontSendNotification);
    ramWarningLabel.setVisible (service.isRamWarningActive());
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
        g.setFont (juce::FontOptions { 13.0f });
        g.drawText (juce::String::fromUTF8 ("keine Eing\xc3\xa4nge"), channelArea,
                    juce::Justification::centred);
    }
}

void CapturePanel::resized()
{
    auto bounds = getLocalBounds().reduced (8, 6);

    // Rechts: Kanal-Zeilen (~30 %), links zwei Control-Zeilen
    channelArea = bounds.removeFromRight (bounds.getWidth() * 30 / 100);
    bounds.removeFromRight (gap);

    channelViewport.setBounds (channelArea.reduced (4));
    layoutChannelRows();

    auto topRow = bounds.removeFromTop (rowHeight);
    bounds.removeFromTop (bounds.getHeight() - rowHeight);  // Rest-Lücke mittig
    auto bottomRow = bounds;

    const auto place = [] (juce::Rectangle<int>& row, juce::Component& component,
                           int width, int gapAfter = gap)
    {
        component.setBounds (row.removeFromLeft (width));
        row.removeFromLeft (gapAfter);
    };

    place (topRow, thresholdSlider, 190);
    place (topRow, autoCalibrateToggle, 130);
    place (topRow, holdSlider, 160);
    place (topRow, preRollSlider, 180);
    place (topRow, bufferSlider, 170);
    ramWarningLabel.setBounds (topRow);

    place (bottomRow, bitDepthCombo, 100);
    place (bottomRow, directoryButton, 100);
    place (bottomRow, releaseAfterExportToggle, 280);
    directoryLabel.setBounds (bottomRow);
}

} // namespace conduit
