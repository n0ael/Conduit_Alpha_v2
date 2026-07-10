#include "GridPage.h"

#include "Modules/ConduitModule.h"
#include "MpeShapingView.h"

namespace conduit
{

GridPage::GridPage (juce::ValueTree rootStateToUse,
                     grid::GridVoiceEngine& engineToUse, grid::MidiDeviceTarget& midiTargetToUse,
                     GridPanelSettings& panelSettingsToUse, UiSettings& uiSettingsToUse)
    : rootState (std::move (rootStateToUse)),
      engine (engineToUse), midiTarget (midiTargetToUse), panelSettings (panelSettingsToUse),
      uiSettings (uiSettingsToUse), keyboard (engineToUse)
{
    addAndMakeVisible (outputCombo);
    addAndMakeVisible (rootTile);
    addAndMakeVisible (scaleTile);
    addAndMakeVisible (releaseAllButton);
    addAndMakeVisible (volumeRibbon);
    addAndMakeVisible (atOffsetRibbon);
    addAndMakeVisible (slideOffsetRibbon);
    addAndMakeVisible (pitchOffsetRibbon);
    addAndMakeVisible (keyboard);
    addChildComponent (dockPanel);   // sichtbar nur wenn offen (setPanelOpen)

    rebuildDeviceList();
    outputCombo.onChange = [this] { handleDeviceSelected(); };

    releaseAllButton.onClick = [this] { engine.allNotesOff(); };

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

    volumeRibbon.onValueChanged = [this] (float value) { engine.setGlobalVolume (value); };

    // Bipolar: Mitte (value01 == 0.5) -> Offset 0, oben -> +1, unten -> -1.
    atOffsetRibbon.onValueChanged = [this] (float value) { engine.setPressureOffset ((value - 0.5f) * 2.0f); };
    slideOffsetRibbon.onValueChanged = [this] (float value) { engine.setSlideOffset ((value - 0.5f) * 2.0f); };

    // Bipolar: Mitte -> 0 HT, oben/unten -> ±kPitchBendOffsetSemitones.
    pitchOffsetRibbon.onValueChanged = [this] (float value)
    {
        engine.setPitchBendOffset ((value - 0.5f) * 2.0f * kPitchBendOffsetSemitones);
    };

    // Editor-Dock-Panel: ein Tab „MPE" mit dem MPE-Shaping-Editor (S2c),
    // Breite/Offen-Zustand aus der Persistenz laden, Live-Resize + Commit
    // verdrahten.
    dockPanel.addTab ("mpe", "MPE",
                      std::make_unique<MpeShapingView> (engine, panelSettings, uiSettings));
    dockPanel.setPanelWidth (panelSettings.getEditorPanelWidth());
    dockPanel.setPanelOpen (panelSettings.isEditorPanelOpen());

    dockPanel.onWidthChanged   = [this] { resized(); };
    dockPanel.onWidthCommitted = [this] (int width) { panelSettings.setEditorPanelWidth (width); };
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

    constexpr int ribbonWidth = 72;

    // Links: Volume, dann Pressure/AT-Offset. Rechts: PitchBend-Offset außen,
    // dann Slide-Offset -- beide Gruppen wachsen vom Rand zur Mitte.
    volumeRibbon.setBounds      (bounds.removeFromLeft  (ribbonWidth));
    atOffsetRibbon.setBounds    (bounds.removeFromLeft  (ribbonWidth));
    pitchOffsetRibbon.setBounds (bounds.removeFromRight (ribbonWidth));
    slideOffsetRibbon.setBounds (bounds.removeFromRight (ribbonWidth));
    keyboard.setBounds (bounds);
}

} // namespace conduit
