#include "GridPage.h"

#include "CcPanel.h"
#include "GridSettingsView.h"
#include "Modules/ConduitModule.h"
#include "MpeShapingView.h"

namespace conduit
{

GridPage::GridPage (juce::ValueTree rootStateToUse,
                     grid::GridVoiceEngine& engineToUse, grid::MidiDeviceTarget& midiTargetToUse,
                     GridPanelSettings& panelSettingsToUse, grid::MpeMidiSink& mpeMidiSinkToUse)
    : rootState (std::move (rootStateToUse)),
      engine (engineToUse), midiTarget (midiTargetToUse), panelSettings (panelSettingsToUse),
      mpeMidiSink (mpeMidiSinkToUse),
      systemControlRowsAtStartup (panelSettingsToUse.getSystemControlRows()),
      // 8×8-Raster der Grid-Page (padLayoutConfig, User 10.07.2026) — Keyboard
      // und ccLayer teilen sich dieselbe Zellgeometrie.
      keyboard (engineToUse, padLayoutConfig()),
      ccLayer (ccModel, padLayoutConfig().cols, padLayoutConfig().rows),
      // System-Controls des XY+Fader-Modus: eigenes 8×N-Zellraster exakt über
      // den oberen N Pad-Reihen (resized), N = systemControlRowsAtStartup.
      systemLayer (systemCcModel, padLayoutConfig().cols, systemControlRowsAtStartup)
{
    addAndMakeVisible (padsModeTile);
    addAndMakeVisible (xyModeTile);
    addAndMakeVisible (releaseAllButton);
    addAndMakeVisible (octaveUpTile);
    addAndMakeVisible (octaveDownTile);
    addAndMakeVisible (atOffsetRibbon);
    addAndMakeVisible (slideOffsetRibbon);
    addAndMakeVisible (pitchOffsetRibbon);
    addChildComponent (modwheelRibbon);   // sichtbar nur bei aktiviertem Modwheel (Block D1)
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

    releaseAllButton.onClick = [this]
    {
        engine.allNotesOff();
        keyboard.clearLatched();   // latched Akkord mit beenden (dessen
                                   // noteOffs verpuffen nach allNotesOff)
    };

    // Oktav-Shift (Block D2): über dem Pitch-Ribbon, geklemmt in
    // GridKeyboardComponent (+-kMaxOctaveShift).
    octaveUpTile.onClick   = [this] { keyboard.octaveUp(); };
    octaveDownTile.onClick = [this] { keyboard.octaveDown(); };

    // Akkord-Speicher (Grid-Page v2, Feature 6): Strip ↔ Keyboard/Memory.
    chordStrip.getConstellation = [this] { return keyboard.constellationNormalized(); };
    chordStrip.onRecall = [this] (int slot) { keyboard.latchConstellation (chordMemory.slot (slot)); };
    chordStrip.onMoveBy = [this] (float dx, float dy) { keyboard.moveLatchedBy (dx, dy); };
    chordStrip.isCcMode = [this] { return ccLayer.isCcMode(); };

    // Session-Skala (Schema 6.2): GridPage liest weiterhin für die
    // Keyboard-Einfärbung mit -- die Anzeige-Kacheln selbst leben seit
    // Block D1 im Settings-Tab (GridSettingsView).
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

    // Modwheel (Block D1, unipolar): sendet CC1 auf dem MPE-Master-Kanal
    // direkt über den MidiDeviceTarget -- kein eigener Sink-Pfad, da nur
    // ein globaler Controller-Wert (kein Voice-Bezug) gebraucht wird.
    modwheelRibbon.onValueChanged = [this] (float value01)
    {
        const auto v = juce::jlimit (0, 127, (int) juce::roundToInt (value01 * 127.0f));
        midiTarget.send (juce::MidiMessage::controllerEvent (
            grid::MpeEncoder::Config{}.memberChannelBase - 1, 1, v));
    };
    modwheelRibbon.setVisible (panelSettings.isModwheelEnabled());

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
    auto mpeView = std::make_unique<MpeShapingView> (engine, panelSettings);
    mpeView->onAxisColourChanged = [this] (Axis axis, juce::Colour colour)
    {
        switch (axis)
        {
            case Axis::Pressure:  atOffsetRibbon.setFillColour (colour); break;
            case Axis::Slide:     slideOffsetRibbon.setFillColour (colour); break;
            case Axis::PitchBend: pitchOffsetRibbon.setFillColour (colour); break;
        }
    };
    // Sensitivity-/Range-Regler (Block A2/A3): die View meldet nur den Wert,
    // GridPage reicht ihn an das GridKeyboardComponent durch (Laufzeit-only,
    // keine Persistenz -- Block K).
    mpeView->onSensitivityChanged = [this] (Axis axis, double sensitivity)
    {
        if (axis == Axis::Pressure)
            keyboard.setPressureSensitivity (sensitivity);
        else if (axis == Axis::Slide)
            keyboard.setSlideSensitivity (sensitivity);
    };
    mpeView->onPitchBendMultiplierChanged = [this] (float multiplier)
    {
        keyboard.setPitchBendMultiplier (multiplier);
    };
    dockPanel.addTab ("mpe", "MPE", std::move (mpeView));

    // Tab 2 „CC" (Grid-Page v2, CC-Baukasten): Werkzeugwahl geht ans
    // Overlay; der CC-Modus (Bearbeiten) gilt bei offenem Panel + aktivem
    // CC-Tab (updateCcMode).
    auto ccPanel = std::make_unique<CcPanel>();
    ccPanel->onToolChanged = [this] (grid::CcTool tool) { ccLayer.setActiveTool (tool); };
    dockPanel.addTab ("cc", "CC", std::move (ccPanel));

    // Tab 3 „Settings" (Block D1): In-Tune Location/Width + Expression Mode
    // (Block B1/B2/B4), Layout-Feinabstimmung (Edit-Grid-Ersatz), Modwheel-
    // Toggle, Performance-Slide-Out (MIDI-Port + Skala, ehemals Top-Row).
    auto settingsView = std::make_unique<GridSettingsView> (
        rootState, midiTarget, panelSettings,
        keyboard.getInTuneLocation(), padLayoutConfig().inTuneWidthPercent,
        mpeMidiSink.expressionMode());
    settingsView->onInTuneLocationChanged = [this] (grid::InTuneLocation location)
    { keyboard.setInTuneLocation (location); };
    settingsView->onInTuneWidthChanged = [this] (float percent)
    { keyboard.setInTuneWidthPercent (percent); };
    settingsView->onExpressionModeChanged = [this] (grid::ExpressionMode mode)
    { mpeMidiSink.setExpressionMode (mode); };
    settingsView->onLayoutSettingsChanged = [this] { applyRibbonWidth(); };
    settingsView->onModwheelToggled = [this] (bool enabled) { modwheelRibbon.setVisible (enabled); resized(); };
    dockPanel.addTab ("settings", "Settings", std::move (settingsView));

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

void GridPage::applyRibbonWidth()
{
    resized();   // liest panelSettings.getRibbonWidthPx() live -- voll dynamisch
}

void GridPage::resized()
{
    auto bounds = getLocalBounds();

    // Editor-Dock-Panel rechts, VOR dem restlichen Layout reserviert --
    // koexistiert mit dem eine Ebene höher (EngineEditor) angedockten
    // Browser-Panel, da dessen removeFromRight bereits in den an GridPage
    // übergebenen bounds steckt.
    dockPanel.setBounds (bounds.removeFromRight (dockPanel.getPreferredWidth()));

    // Block D2: die frühere MIDI-Port-/Skala-Top-Row entfällt zugunsten des
    // Performance-Slide-Outs (Settings-Tab, GridSettingsView) — nur noch
    // die Layout-Modus-Kacheln oben links, kompakte 28-px-Zeile.
    auto topStrip = bounds.removeFromTop (28);
    constexpr int modeTileSide = 24;
    padsModeTile.setBounds (topStrip.getX() + 4, topStrip.getY() + 2, modeTileSide, modeTileSide);
    xyModeTile.setBounds (padsModeTile.getRight() + 4, topStrip.getY() + 2, modeTileSide, modeTileSide);

    const auto ribbonWidth = panelSettings.getRibbonWidthPx();   // Block D1, live

    // Block D2: Pitch-Fader-Stapel — Oktav-Buttons darüber, Release All
    // darunter (ersetzt die frühere Top-Row-Platzierung beider Elemente).
    auto pitchColumn = bounds.removeFromLeft (ribbonWidth);
    auto octaveRow = pitchColumn.removeFromTop (28);
    const auto octaveTileWidth = octaveRow.getWidth() / 2;
    octaveDownTile.setBounds (octaveRow.removeFromLeft (octaveTileWidth));
    octaveUpTile.setBounds (octaveRow);
    auto releaseRow = pitchColumn.removeFromBottom (36);
    releaseAllButton.setBounds (releaseRow.reduced (2));
    pitchOffsetRibbon.setBounds (pitchColumn);

    // Modwheel (Block D1): eigene Spalte direkt neben Pitch, Breite nur
    // reserviert, wenn aktiviert.
    if (modwheelRibbon.isVisible())
        modwheelRibbon.setBounds (bounds.removeFromLeft (ribbonWidth));

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

    // System-Controls (XY+Fader-Modus): exakt die oberen systemControlRowsAtStartup
    // Zellreihen der Keyboard-Fläche — IMMER positioniert, die Sichtbarkeit
    // entscheidet. Zeilenzahl fix seit Konstruktion (CcControlLayer::rows
    // ist const, siehe systemControlRowsAtStartup-Kommentar im Header).
    const auto systemHeight = juce::roundToInt ((float) bounds.getHeight()
                                  * (float) systemControlRowsAtStartup
                                  / (float) padLayoutConfig().rows);
    systemLayer.setBounds (bounds.withHeight (systemHeight));

    if (bounds.getHeight() > 0)
        chordStrip.setSurfaceAspect ((float) bounds.getWidth() / (float) bounds.getHeight());
}

} // namespace conduit
