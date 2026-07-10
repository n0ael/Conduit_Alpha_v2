#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GridPanelSettings.h"
#include "Core/GridVoiceEngine.h"
#include "Core/MidiDeviceTarget.h"
#include "Core/UiSettings.h"
#include "EditorDockPanel.h"
#include "ExpressionRibbon.h"
#include "GridKeyboardComponent.h"
#include "PushTiles.h"
#include "Util/ScaleQuantizer.h"

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
    EngineEditor). Genau ein Tab „MPE" mit MpeShapingView (S2c) -- drei
    Kurven (Pressure/Slide/PitchBend) + Live-Noten-Kreise, reine Anzeige
    (Touch-Bearbeitung folgt in S2c-2). Toggle über einen eigenen
    TransportBar-Button (setDockPanelOpen), Breite/Offen-Zustand persistiert
    über GridPanelSettings (App-Zustand, Muster MeterSettings).

    Session-Skala (Grid-Page v2, Design-Mock): Root- und Skala-Kachel in der
    Top-Row zykeln scaleRoot/scaleType per Tap; geschrieben wird NUR in den
    Root-ValueTree, die Anzeige (Kacheln + Pad-Einfärbung des Keyboards)
    folgt dem ValueTree-Listener — so kommen auch Änderungen aus der
    TransportBar an (UI bindet nie an den Processor, CLAUDE.md 5.3).
*/
class GridPage final : public juce::Component,
                       private juce::ValueTree::Listener
{
public:
    GridPage (juce::ValueTree rootStateToUse,
              grid::GridVoiceEngine& engineToUse, grid::MidiDeviceTarget& midiTargetToUse,
              GridPanelSettings& panelSettingsToUse, UiSettings& uiSettingsToUse);
    ~GridPage() override;

    void resized() override;

    /** Toggle vom TransportBar-Button (unabhängig vom Browser) -- schaltet
        das rechte Editor-Dock-Panel und persistiert den Zustand. */
    void setDockPanelOpen (bool shouldBeOpen) noexcept;
    [[nodiscard]] bool isDockPanelOpen() const noexcept { return dockPanel.isPanelOpen(); }

    //==========================================================================
    // Kachel-Zyklen der Skala-Anzeige (Session-Skala, Design-Mock Grid-Page
    // v2) — pure functions, testbar ohne GridPage-Instanz.

    /** Root-Kachel: C→C#→…→B→C. */
    [[nodiscard]] static int nextScaleRoot (int rootNote) noexcept;

    /** Skala-Kachel: chromatic→major→minor→pentatonic→chromatic. */
    [[nodiscard]] static ScaleType nextScaleType (ScaleType type) noexcept;

    /** Notenname der Root-Kachel ("C" … "B"). */
    [[nodiscard]] static juce::String noteNameFor (int rootNote);

    /** Anzeigename der Skala-Kachel — toString mit großem Anfangsbuchstaben
        ("Chromatic", "Major", "Minor", "Pentatonic"). */
    [[nodiscard]] static juce::String scaleDisplayNameFor (ScaleType type);

private:
    void rebuildDeviceList();
    void handleDeviceSelected();

    /** Liest scaleRoot/scaleType aus dem Root-Tree und aktualisiert Kacheln
        + Keyboard-Einfärbung — Anzeige folgt IMMER dem ValueTree (5.3). */
    void refreshScaleFromState();

    void valueTreePropertyChanged (juce::ValueTree& tree,
                                   const juce::Identifier& property) override;

    // Bereich des PitchBend-Offset-Ribbons: Mitte = 0, ±Ende = ±12 Halbtöne.
    // Spätere 1–96-Range-UI ersetzt diese Konstante.
    static constexpr float kPitchBendOffsetSemitones = 12.0f;

    juce::ValueTree rootState;  // ref-counted Handle (Session-Skala), nie der Processor (5.3)
    grid::GridVoiceEngine& engine;
    grid::MidiDeviceTarget& midiTarget;
    GridPanelSettings& panelSettings;
    UiSettings& uiSettings;
    juce::Array<juce::MidiDeviceInfo> devices;

    juce::ComboBox outputCombo;
    push::TextTile rootTile  { "C" };            // Session-Skala: Grundton (Tap = weiterzykeln)
    push::TextTile scaleTile { "Chromatic" };    // Session-Skala: Typ (Tap = weiterzykeln)
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
