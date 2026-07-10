#include "GridPage.h"

#include "CcPanel.h"
#include "Modules/ConduitModule.h"
#include "MpeShapingView.h"

namespace conduit
{

GridPage::GridPage (juce::ValueTree rootStateToUse,
                     grid::GridVoiceEngine& engineToUse, grid::MidiDeviceTarget& midiTargetToUse,
                     GridPanelSettings& panelSettingsToUse, UiSettings& uiSettingsToUse)
    : rootState (std::move (rootStateToUse)),
      engine (engineToUse), midiTarget (midiTargetToUse), panelSettings (panelSettingsToUse),
      uiSettings (uiSettingsToUse),
      // 8×8-Raster der Grid-Page (padLayoutConfig, User 10.07.2026) — Keyboard
      // und ccLayer teilen sich dieselbe Zellgeometrie.
      keyboard (engineToUse, padLayoutConfig()),
      ccLayer (ccModel, padLayoutConfig().cols, padLayoutConfig().rows),
      // System-Controls des XY+Fader-Modus: eigenes 8×2-Zellraster exakt über
      // den oberen zwei Pad-Reihen (resized).
      systemLayer (systemCcModel, padLayoutConfig().cols, kSystemControlRows)
{
    addAndMakeVisible (outputCombo);
    addAndMakeVisible (rootTile);
    addAndMakeVisible (scaleTile);
    addAndMakeVisible (padsModeTile);
    addAndMakeVisible (xyModeTile);
    addAndMakeVisible (releaseAllButton);
    addAndMakeVisible (atOffsetRibbon);
    addAndMakeVisible (slideOffsetRibbon);
    addAndMakeVisible (pitchOffsetRibbon);
    addAndMakeVisible (keyboard);
    addAndMakeVisible (ccLayer);     // NACH keyboard hinzugefügt = liegt darüber
    addChildComponent (systemLayer); // NACH ccLayer = darüber: System-Controls
                                     // gewinnen die Hit-Tests auch im CC-Tab-
                                     // Modus (dort SPIELBAR — TODO(design));
                                     // sichtbar nur im XY+Fader-Modus.
    addAndMakeVisible (chordStrip);  // eigene Spalte NEBEN Keyboard/ccLayer
    addChildComponent (dockPanel);   // sichtbar nur wenn offen (setPanelOpen)

    // Layout-Modus-Kacheln (User 10.07.2026): 64 Pads (Push-Style) vs.
    // XY+Fader über den oberen zwei Pad-Reihen — Tap setzt Persistenz und
    // wendet den Modus an. systemLayer bleibt IMMER im Play-Modus
    // (setCcMode(false) ist der Ctor-Default, es gibt keinen Umschalter).
    padsModeTile.onClick = [this] { setLayoutMode (GridPanelSettings::GridLayoutMode::fullPads); };
    xyModeTile.onClick   = [this] { setLayoutMode (GridPanelSettings::GridLayoutMode::xyFaders); };

    rebuildDeviceList();
    outputCombo.onChange = [this] { handleDeviceSelected(); };

    releaseAllButton.onClick = [this]
    {
        engine.allNotesOff();
        keyboard.clearLatched();   // latched Akkord mit beenden (dessen
                                   // noteOffs verpuffen nach allNotesOff)
    };

    // Akkord-Speicher (Grid-Page v2, Feature 6): Strip ↔ Keyboard/Memory.
    chordStrip.getConstellation = [this] { return keyboard.constellationNormalized(); };
    chordStrip.onRecall = [this] (int slot) { keyboard.latchConstellation (chordMemory.slot (slot)); };
    chordStrip.onMoveBy = [this] (float dx, float dy) { keyboard.moveLatchedBy (dx, dy); };
    chordStrip.isCcMode = [this] { return ccLayer.isCcMode(); };

    // Session-Skala (Schema 6.2): Taps zykeln Root/Typ NUR über den
    // Root-ValueTree — die Anzeige aktualisiert der Listener, damit auch
    // Änderungen aus der TransportBar ankommen (5.3).
    rootTile.onClick = [this]
    {
        const auto current = juce::jlimit (0, 11, (int) rootState.getProperty (id::scaleRoot, 0));
        rootState.setProperty (id::scaleRoot, nextScaleRoot (current), nullptr);
    };

    scaleTile.onClick = [this]
    {
        const auto current = scaleTypeFromString (rootState.getProperty (id::scaleType).toString());
        rootState.setProperty (id::scaleType, toString (nextScaleType (current)), nullptr);
    };

    rootState.addListener (this);
    refreshScaleFromState();   // Initialwert beim Konstruieren lesen

    // Bipolar: Mitte (value01 == 0.5) -> Offset 0, oben -> +1, unten -> -1.
    atOffsetRibbon.onValueChanged = [this] (float value) { engine.setPressureOffset ((value - 0.5f) * 2.0f); };
    slideOffsetRibbon.onValueChanged = [this] (float value) { engine.setSlideOffset ((value - 0.5f) * 2.0f); };

    // Bipolar: Mitte -> 0 HT, oben/unten -> ±kPitchBendOffsetSemitones.
    pitchOffsetRibbon.onValueChanged = [this] (float value)
    {
        engine.setPitchBendOffset ((value - 0.5f) * 2.0f * kPitchBendOffsetSemitones);
    };

    // Achsen-Farben (Grid-Page v2): user-konfigurierbar und persistent in
    // GridPanelSettings — Initialwerte von dort statt hart kodiert.
    using Axis = grid::GridVoiceEngine::Axis;
    pitchOffsetRibbon.setFillColour (panelSettings.getAxisColour (Axis::PitchBend));
    atOffsetRibbon.setFillColour    (panelSettings.getAxisColour (Axis::Pressure));
    slideOffsetRibbon.setFillColour (panelSettings.getAxisColour (Axis::Slide));

    // Editor-Dock-Panel: ein Tab „MPE" mit dem MPE-Shaping-Editor (S2c),
    // Breite/Offen-Zustand aus der Persistenz laden, Live-Resize + Commit
    // verdrahten. Farbwahl in der MpeShapingView (Quick-Swatch/Picker)
    // aktualisiert die Ribbon-Füllfarben live (Persistenz macht die View).
    auto mpeView = std::make_unique<MpeShapingView> (engine, panelSettings, uiSettings);
    mpeView->onAxisColourChanged = [this] (Axis axis, juce::Colour colour)
    {
        switch (axis)
        {
            case Axis::Pressure:  atOffsetRibbon.setFillColour (colour); break;
            case Axis::Slide:     slideOffsetRibbon.setFillColour (colour); break;
            case Axis::PitchBend: pitchOffsetRibbon.setFillColour (colour); break;
        }
    };
    dockPanel.addTab ("mpe", "MPE", std::move (mpeView));

    // Tab 2 „CC" (Grid-Page v2, CC-Baukasten): Werkzeugwahl geht ans
    // Overlay; der CC-Modus (Bearbeiten) gilt bei offenem Panel + aktivem
    // CC-Tab (updateCcMode).
    auto ccPanel = std::make_unique<CcPanel>();
    ccPanel->onToolChanged = [this] (grid::CcTool tool) { ccLayer.setActiveTool (tool); };
    dockPanel.addTab ("cc", "CC", std::move (ccPanel));

    dockPanel.setPanelWidth (panelSettings.getEditorPanelWidth());
    dockPanel.setPanelOpen (panelSettings.isEditorPanelOpen());

    dockPanel.onWidthChanged   = [this] { resized(); };
    dockPanel.onWidthCommitted = [this] (int width) { panelSettings.setEditorPanelWidth (width); };
    dockPanel.onActiveTabChanged = [this] (const juce::String&) { updateCcMode(); };

    updateCcMode();   // Initialzustand (Panel-Open aus panelSettings, aktiver Tab "mpe")
    applyLayoutMode();   // Initialer Layout-Modus aus der Persistenz
}

GridPage::~GridPage()
{
    rootState.removeListener (this);
}

//==============================================================================
int GridPage::nextScaleRoot (int rootNote) noexcept
{
    return ((rootNote + 1) % 12 + 12) % 12;
}

ScaleType GridPage::nextScaleType (ScaleType type) noexcept
{
    switch (type)
    {
        case ScaleType::chromatic:  return ScaleType::major;
        case ScaleType::major:      return ScaleType::minor;
        case ScaleType::minor:      return ScaleType::pentatonic;
        case ScaleType::pentatonic: return ScaleType::chromatic;
    }

    jassertfalse;
    return ScaleType::chromatic;
}

juce::String GridPage::noteNameFor (int rootNote)
{
    static const char* const noteNames[] = { "C", "C#", "D", "D#", "E", "F",
                                             "F#", "G", "G#", "A", "A#", "B" };
    return noteNames[juce::jlimit (0, 11, rootNote)];
}

juce::String GridPage::scaleDisplayNameFor (ScaleType type)
{
    const auto name = toString (type);
    return name.substring (0, 1).toUpperCase() + name.substring (1);
}

grid::PadGridLayout::Config GridPage::padLayoutConfig() noexcept
{
    // 64 Pads (8×8, Push-Style, User 10.07.2026): die Config-Defaults bleiben
    // 8×4 — nur die Grid-Page setzt rows explizit. lowestNote 48 unverändert,
    // die neuen Reihen wachsen nach OBEN dazu (+5 HT/Reihe).
    grid::PadGridLayout::Config config;
    config.rows = 8;
    return config;
}

void GridPage::refreshScaleFromState()
{
    const auto rootNote = juce::jlimit (0, 11, (int) rootState.getProperty (id::scaleRoot, 0));
    const auto type = scaleTypeFromString (rootState.getProperty (id::scaleType).toString());

    rootTile.setText (noteNameFor (rootNote));
    scaleTile.setText (scaleDisplayNameFor (type));
    keyboard.setScale (rootNote, type);
}

void GridPage::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == rootState && (property == id::scaleRoot || property == id::scaleType))
        refreshScaleFromState();
}

//==============================================================================
void GridPage::setDockPanelOpen (bool shouldBeOpen) noexcept
{
    dockPanel.setPanelOpen (shouldBeOpen);
    panelSettings.setEditorPanelOpen (shouldBeOpen);
    updateCcMode();
    resized();
}

void GridPage::updateCcMode()
{
    ccLayer.setCcMode (dockPanel.isPanelOpen() && dockPanel.getActiveTabId() == "cc");
}

void GridPage::setLayoutMode (GridPanelSettings::GridLayoutMode newMode)
{
    panelSettings.setGridLayoutMode (newMode);
    applyLayoutMode();
}

void GridPage::applyLayoutMode()
{
    const auto xyFaders = panelSettings.getGridLayoutMode()
                              == GridPanelSettings::GridLayoutMode::xyFaders;

    padsModeTile.setActive (! xyFaders);
    xyModeTile.setActive (xyFaders);

    // Sauberer Zustand bei jedem Wechsel: Modell leeren, im XY+Fader-Modus
    // frisch bestücken — der Werte-Reset ist akzeptiert (TODO(design):
    // Persistenz der Control-Werte über den Moduswechsel).
    systemCcModel.clear();

    if (xyFaders)
        grid::buildXyFaderLayout (systemCcModel);

    systemLayer.setVisible (xyFaders);
    systemLayer.repaint();
    resized();
}

void GridPage::rebuildDeviceList()
{
    devices = grid::MidiDeviceTarget::availableDevices();

    outputCombo.clear (juce::dontSendNotification);
    outputCombo.addItem ("Kein Ausgang", 1);

    for (int i = 0; i < devices.size(); ++i)
        outputCombo.addItem (devices.getReference (i).name, i + 2);

    outputCombo.setSelectedId (1, juce::dontSendNotification);
}

void GridPage::handleDeviceSelected()
{
    const auto selectedId = outputCombo.getSelectedId();

    if (selectedId <= 1)
    {
        midiTarget.closeDevice();
        return;
    }

    const auto index = selectedId - 2;
    if (index >= 0 && index < devices.size())
        midiTarget.openDevice (devices.getReference (index).identifier);
}

void GridPage::resized()
{
    auto bounds = getLocalBounds();

    // Editor-Dock-Panel rechts, VOR dem restlichen Layout reserviert --
    // koexistiert mit dem eine Ebene höher (EngineEditor) angedockten
    // Browser-Panel, da dessen removeFromRight bereits in den an GridPage
    // übergebenen bounds steckt.
    dockPanel.setBounds (bounds.removeFromRight (dockPanel.getPreferredWidth()));

    auto topRow = bounds.removeFromTop (32);
    const auto comboArea = topRow.removeFromLeft (200).reduced (8, 4);
    outputCombo.setBounds (comboArea);
    releaseAllButton.setBounds (topRow.removeFromRight (120).reduced (8, 4));

    // Session-Skala-Kacheln rechts neben dem MIDI-Dropdown, in der
    // 24-px-Kachelhöhe der Top-Row (Design-Mock: Root 44x24 mit 6 px
    // Abstand, Skala 104x24 mit 4 px Abstand).
    rootTile.setBounds  (comboArea.getRight() + 6, comboArea.getY(), 44, comboArea.getHeight());
    scaleTile.setBounds (rootTile.getRight() + 4, comboArea.getY(), 104, comboArea.getHeight());

    // Layout-Modus-Kacheln (User 10.07.2026): 64 Pads / XY+Fader — LINKS
    // neben Release-All, 24×24 wie die Nachbarkacheln, 4 px Abstand.
    const auto modeTileSide = comboArea.getHeight();
    xyModeTile.setBounds (releaseAllButton.getX() - 8 - modeTileSide,
                          comboArea.getY(), modeTileSide, modeTileSide);
    padsModeTile.setBounds (xyModeTile.getX() - 4 - modeTileSide,
                            comboArea.getY(), modeTileSide, modeTileSide);

    constexpr int ribbonWidth = 72;

    // Design-Mock Grid-Page v2: links Pitch in voller Höhe, rechts EINE
    // Spalte mit Pressure (oben) über Slide (unten) je halber Höhe.
    pitchOffsetRibbon.setBounds (bounds.removeFromLeft (ribbonWidth));

    auto rightColumn = bounds.removeFromRight (ribbonWidth);
    atOffsetRibbon.setBounds    (rightColumn.removeFromTop (rightColumn.getHeight() / 2));
    slideOffsetRibbon.setBounds (rightColumn);

    // Akkord-Speicher-Strip (Grid-Page v2, Feature 6) zwischen Pad-Raster
    // und rechter Ribbon-Spalte — Mock-Formel: quadratische Slots aus der
    // verbleibenden Höhe (8 Slots, 2 px Gap, 1 px Padding).
    const auto stripW = juce::jmax (40, juce::roundToInt (((float) bounds.getHeight() - 16.0f) / 8.0f) + 2);
    chordStrip.setBounds (bounds.removeFromRight (stripW));

    keyboard.setBounds (bounds);
    ccLayer.setBounds (bounds);   // Overlay exakt über den Keyboard-Bounds

    // System-Controls (XY+Fader-Modus): exakt die oberen zwei Zellreihen der
    // Keyboard-Fläche — IMMER positioniert, die Sichtbarkeit entscheidet.
    const auto systemHeight = juce::roundToInt ((float) bounds.getHeight()
                                  * (float) kSystemControlRows
                                  / (float) padLayoutConfig().rows);
    systemLayer.setBounds (bounds.withHeight (systemHeight));

    if (bounds.getHeight() > 0)
        chordStrip.setSurfaceAspect ((float) bounds.getWidth() / (float) bounds.getHeight());
}

} // namespace conduit
