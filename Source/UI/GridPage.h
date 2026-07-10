#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "CcControlLayer.h"
#include "Core/CcControlModel.h"
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
    Grid-Page (Ω, M1 Teil 3 — erster spielbarer Ton; Grid-Page v2 —
    Ribbon-Umbau nach Design-Mock): GridKeyboardComponent (Hauptfläche)
    flankiert von drei bipolaren Rand-Ribbons (Mitte = neutral) — links
    „Pitch" in voller Höhe (±12 Halbtöne,
    GridVoiceEngine::setPitchBendOffset, grün), rechts EINE Spalte mit
    „Pressure" oben (GridVoiceEngine::setPressureOffset, orange) über
    „Slide" unten (GridVoiceEngine::setSlideOffset, cyan) — plus ein
    minimales MIDI-Out-Port-Dropdown und ein Release-All-Button
    (GridVoiceEngine::allNotesOff). Das frühere Volume-Ribbon ist
    entfallen; GridVoiceEngine::setGlobalVolume bleibt für Tests/Zukunft
    bestehen.

    Rechtes Editor-Dock-Panel (S2-Vorstufe MPE-Shaping): EditorDockPanel
    dockt via bounds.removeFromRight (dockPanel.getPreferredWidth()) --
    koexistiert mit dem Browser-Panel (das dockt eine Ebene höher im
    EngineEditor). Tab „MPE" mit MpeShapingView (S2c) -- drei
    Kurven (Pressure/Slide/PitchBend) + Live-Noten-Kreise, reine Anzeige
    (Touch-Bearbeitung folgt in S2c-2). Toggle über einen eigenen
    TransportBar-Button (setDockPanelOpen), Breite/Offen-Zustand persistiert
    über GridPanelSettings (App-Zustand, Muster MeterSettings).

    CC-Baukasten (Grid-Page v2): zweiter Tab „CC" (CcPanel, Werkzeuge
    Fader/Push/Toggle/XY) + CcControlLayer als Overlay exakt über den
    Keyboard-Bounds (nach keyboard deklariert/hinzugefügt = darüber).
    CC-Modus (Bearbeiten) gilt, wenn das Dock-Panel offen ist UND der
    aktive Tab „cc" — aktualisiert in setDockPanelOpen und über
    EditorDockPanel::onActiveTabChanged (updateCcMode).

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

    /** CC-Modus des Overlays = (Dock-Panel offen) UND (aktiver Tab "cc"). */
    void updateCcMode();

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
    ExpressionRibbon atOffsetRibbon      { "Pressure", true };  // bipolar
    ExpressionRibbon slideOffsetRibbon   { "Slide", true };     // bipolar
    ExpressionRibbon pitchOffsetRibbon   { "Pitch", true };     // bipolar
    GridKeyboardComponent keyboard;
    grid::CcControlModel ccModel;     // CC-Baukasten (Grid-Page v2)
    CcControlLayer ccLayer;           // Overlay ÜBER dem Keyboard (nach keyboard deklariert)
    EditorDockPanel dockPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridPage)
};

} // namespace conduit
