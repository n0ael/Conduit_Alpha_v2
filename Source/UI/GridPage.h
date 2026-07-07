#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GridPanelSettings.h"
#include "Core/GridVoiceEngine.h"
#include "Core/MidiDeviceTarget.h"
#include "EditorDockPanel.h"
#include "ExpressionRibbon.h"
#include "GridKeyboardComponent.h"
#include "PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    Grid-Page (Ω, M1 Teil 3 — erster spielbarer Ton; M1b-3 — globale
    Ausdrucksebene + Panic; M1b-4 — bipolarer Pressure-Offset; M1b-6 —
    Slide-/PitchBend-Offset-Ribbons): GridKeyboardComponent (Hauptfläche)
    flankiert von vier Rand-Ribbons — links Volume (unipolar, MPE-Master-
    Kanal CC7, GridVoiceEngine::setGlobalVolume) und Pressure/AT-Offset
    (bipolar, Mitte = neutral, GridVoiceEngine::setPressureOffset), rechts
    Slide-Offset (bipolar, GridVoiceEngine::setSlideOffset) und PitchBend-
    Offset (bipolar, ±12 Halbtöne, GridVoiceEngine::setPitchBendOffset) —
    plus ein minimales MIDI-Out-Port-Dropdown und ein Release-All-Button
    (GridVoiceEngine::allNotesOff). Kein Feinschliff darüber hinaus — reine
    Funktion, bis der Touch-Controller-Baukasten (CLAUDE.md 14 ADR) als
    eigener Meilenstein folgt.

    Rechtes Editor-Dock-Panel (S2-Vorstufe MPE-Shaping): EditorDockPanel
    dockt via bounds.removeFromRight (dockPanel.getPreferredWidth()) --
    koexistiert mit dem Browser-Panel (das dockt eine Ebene höher im
    EngineEditor). Genau ein Tab „MPE" mit Platzhalter-Inhalt (kein
    Kurven-Editor -- das ist S2c). Toggle über einen eigenen TransportBar-
    Button (setDockPanelOpen), Breite/Offen-Zustand persistiert über
    GridPanelSettings (App-Zustand, Muster MeterSettings).
*/
class GridPage final : public juce::Component
{
public:
    GridPage (grid::GridVoiceEngine& engineToUse, grid::MidiDeviceTarget& midiTargetToUse,
              GridPanelSettings& panelSettingsToUse);

    void resized() override;

    /** Toggle vom TransportBar-Button (unabhängig vom Browser) -- schaltet
        das rechte Editor-Dock-Panel und persistiert den Zustand. */
    void setDockPanelOpen (bool shouldBeOpen) noexcept;
    [[nodiscard]] bool isDockPanelOpen() const noexcept { return dockPanel.isPanelOpen(); }

private:
    void rebuildDeviceList();
    void handleDeviceSelected();

    /** Platzhalter-Inhalt des „MPE"-Tabs -- kein Editor-Inhalt (S2c). */
    class MpePlaceholder final : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override;
    };

    // Bereich des PitchBend-Offset-Ribbons: Mitte = 0, ±Ende = ±12 Halbtöne.
    // Spätere 1–96-Range-UI ersetzt diese Konstante.
    static constexpr float kPitchBendOffsetSemitones = 12.0f;

    grid::GridVoiceEngine& engine;
    grid::MidiDeviceTarget& midiTarget;
    GridPanelSettings& panelSettings;
    juce::Array<juce::MidiDeviceInfo> devices;

    juce::ComboBox outputCombo;
    push::TextTile releaseAllButton { "Release All", push::colours::ledRed };
    ExpressionRibbon volumeRibbon        { "VOL" };
    ExpressionRibbon atOffsetRibbon      { "AT", true };   // bipolar
    ExpressionRibbon slideOffsetRibbon   { "SLD", true };  // bipolar
    ExpressionRibbon pitchOffsetRibbon   { "BND", true };  // bipolar
    GridKeyboardComponent keyboard;
    EditorDockPanel dockPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridPage)
};

} // namespace conduit
