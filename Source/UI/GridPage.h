#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "CcControlLayer.h"
#include "ChordMemoryStrip.h"
#include "Core/CcControlModel.h"
#include "Core/ChordMemory.h"
#include "Core/GridPanelSettings.h"
#include "Core/GridVoiceEngine.h"
#include "Core/MidiDeviceTarget.h"
#include "Core/MpeMidiSink.h"
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
    Ribbon-Umbau nach Design-Mock; Block D2 — Performance-Layout-Umbau):
    GridKeyboardComponent (Hauptfläche) flankiert von bipolaren Rand-
    Ribbons (Mitte = neutral) — links „Pitch" (±12 Halbtöne,
    GridVoiceEngine::setPitchBendOffset, grün) mit Oktav-Buttons darüber
    (GridKeyboardComponent::octaveUp/Down) und Release-All darunter
    (GridVoiceEngine::allNotesOff), rechts EINE Spalte mit „Pressure" oben
    (GridVoiceEngine::setPressureOffset, orange) über „Slide" unten
    (GridVoiceEngine::setSlideOffset, cyan), optional ein unipolares
    Modwheel-Ribbon direkt neben Pitch (Block D1, sendet CC1 über den
    MidiDeviceTarget). Ribbon-Breite gemeinsam einstellbar
    (GridPanelSettings::ribbonWidthPx, Settings-Tab). Die frühere MIDI-
    Port-/Skala-Top-Row ist ins Settings-Tab umgezogen (Performance-Slide-
    Out, GridSettingsView) — nur die Layout-Modus-Kacheln bleiben oben
    links. Das frühere Volume-Ribbon ist entfallen; GridVoiceEngine::
    setGlobalVolume bleibt für Tests/Zukunft bestehen.

    Rechtes Editor-Dock-Panel (S2-Vorstufe MPE-Shaping): EditorDockPanel
    dockt via bounds.removeFromRight (dockPanel.getPreferredWidth()) --
    koexistiert mit dem Browser-Panel (das dockt eine Ebene höher im
    EngineEditor). Tab „MPE" mit MpeShapingView (S2c) -- drei touch-
    editierbare Kurven (Pressure/Slide/PitchBend) + Live-Noten-Kreise, je
    Achse eine Detailspalte mit Sensitivity-Regler bzw. PitchBend-Range-
    Multiplikator (Block A2/A3), Offset-Schloss und Achsfarbe. Toggle über
    einen eigenen TransportBar-Button (setDockPanelOpen), Breite/Offen-
    Zustand persistiert über GridPanelSettings (App-Zustand, Muster
    MeterSettings).

    CC-Baukasten (Grid-Page v2): zweiter Tab „CC" (CcPanel, Werkzeuge
    Fader/Push/Toggle/XY) + CcControlLayer als Overlay exakt über den
    Keyboard-Bounds (nach keyboard deklariert/hinzugefügt = darüber).
    CC-Modus (Bearbeiten) gilt, wenn das Dock-Panel offen ist UND der
    aktive Tab „cc" — aktualisiert in setDockPanelOpen und über
    EditorDockPanel::onActiveTabChanged (updateCcMode).

    Settings-Tab (Block D1, GridSettingsView): dritter Tab -- Performance-
    Slide-Out (MIDI-Ausgangsport + Session-Skala-Kacheln, ehemals Top-Row),
    In-Tune Location/Width (Block B1/B2), Expression Mode MPE/Poly-AT/
    Mono-AT (Block B4, GridVoiceEngine unbeteiligt -- reicht direkt an
    MpeMidiSink::setExpressionMode), Layout-Feinabstimmung (XY-Zeilen +
    Fader-Breite als Zahlenfelder, Ersatz für die freie Drag-Resize-Fläche
    der Roadmap-Beschreibung, TODO(design)), Modwheel-Toggle. Die View
    bindet selbst an rootState (eigener ValueTree::Listener für die
    Skala-Kacheln) und meldet alles andere über Callbacks, exakt wie
    MpeShapingView/CcPanel.

    Session-Skala (Grid-Page v2, Design-Mock): GridPage selbst hört nur noch
    für die Keyboard-Einfärbung auf den Root-ValueTree (refreshScaleFromState)
    — die Anzeige-Kacheln leben seit Block D1 im Settings-Tab. Geschrieben
    wird NUR in den Root-ValueTree (UI bindet nie an den Processor,
    CLAUDE.md 5.3).

    Pad-Layout-Modi (User 10.07.2026): das Raster ist 8×8 (64 Pads,
    Push-Style, padLayoutConfig()). Zwei IconTiles (gridMpe/gridMpeXy) oben
    links schalten zwischen „64 Pads" und „XY+Fader" um (persistent,
    GridPanelSettings::gridLayoutMode). Im XY+Fader-Modus überdeckt ein
    eigener systemLayer (CcControlLayer über systemCcModel,
    8×systemControlRowsAtStartup-Zellraster, IMMER Play-Modus) die oberen
    Pad-Reihen mit fester Bestückung (grid::buildXyFaderLayout: 1× XY +
    6 Fader) — das 8×8-Noten-Mapping des Keyboards bleibt unverändert, die
    überdeckten Pads sind schlicht unspielbar. Der systemLayer liegt ÜBER
    dem User-ccLayer (nach ihm hinzugefügt) und gewinnt dessen Hit-Tests
    auch im CC-Tab-Modus — dass die System-Controls dort SPIELBAR bleiben,
    ist akzeptiert (TODO(design)).
*/
class GridPage final : public juce::Component,
                       private juce::ValueTree::Listener
{
public:
    GridPage (juce::ValueTree rootStateToUse,
              grid::GridVoiceEngine& engineToUse, grid::MidiDeviceTarget& midiTargetToUse,
              GridPanelSettings& panelSettingsToUse, grid::MpeMidiSink& mpeMidiSinkToUse);
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

    /** Pad-Raster der Grid-Page: 8×8 (64 Pads, Push-Style, User 10.07.2026).
        Die PadGridLayout-Config-Defaults bleiben bewusst 8×4 (andere
        Nutzer/Tests) — nur die Grid-Page setzt rows explizit; lowestNote 48
        unverändert, die neuen Reihen wachsen nach OBEN dazu. */
    [[nodiscard]] static grid::PadGridLayout::Config padLayoutConfig() noexcept;

private:
    /** Liest scaleRoot/scaleType aus dem Root-Tree und aktualisiert die
        Keyboard-Einfärbung — Anzeige folgt IMMER dem ValueTree (5.3). Die
        Skala-Kacheln selbst leben seit Block D1 im Settings-Tab
        (GridSettingsView, eigener Listener dort). */
    void refreshScaleFromState();

    /** CC-Modus des Overlays = (Dock-Panel offen) UND (aktiver Tab "cc"). */
    void updateCcMode();

    /** Ribbon-Breite (Block D1 "Fader-Breiten", GridPanelSettings-Wert) hat
        sich geändert -- ruft nur resized() auf (voll live, im Gegensatz zur
        systemControlRows-Zeilenzahl, siehe systemControlRowsAtStartup). */
    void applyRibbonWidth();

    /** Modus-Kachel-Tap: persistiert den Layout-Modus und wendet ihn an. */
    void setLayoutMode (GridPanelSettings::GridLayoutMode newMode);

    /** Kachel-Aktivzustand + Sichtbarkeit/Bestückung der System-Controls
        (XY+Fader-Modus) — resized() positioniert den systemLayer immer,
        die Sichtbarkeit entscheidet. */
    void applyLayoutMode();

    void valueTreePropertyChanged (juce::ValueTree& tree,
                                   const juce::Identifier& property) override;

    // Bereich des PitchBend-Offset-Ribbons: Mitte = 0, ±Ende = ±12 Halbtöne.
    // Spätere 1–96-Range-UI ersetzt diese Konstante.
    static constexpr float kPitchBendOffsetSemitones = 12.0f;

    juce::ValueTree rootState;  // ref-counted Handle (Session-Skala), nie der Processor (5.3)
    grid::GridVoiceEngine& engine;
    grid::MidiDeviceTarget& midiTarget;
    GridPanelSettings& panelSettings;
    grid::MpeMidiSink& mpeMidiSink;   // Block D1: Expression-Mode-Umschaltung (Settings-Tab)

    // XY+Fader-Modus: Zeilenzahl des systemLayer -- CcControlLayer::rows ist
    // const (kein Laufzeit-Resize), daher bei GridPage-Konstruktion aus
    // GridPanelSettings gecacht. Ein Wechsel im Settings-Tab persistiert
    // sofort, wirkt aber erst beim naechsten Neuaufbau der Grid-Page
    // (TODO(design): echtes Laufzeit-Resize braucht CcControlLayer-Umbau).
    const int systemControlRowsAtStartup;

    push::IconTile padsModeTile { push::Icon::gridMpe,   "padLayoutFullPads" };  // 64 Pads
    push::IconTile xyModeTile   { push::Icon::gridMpeXy, "padLayoutXyFaders" };  // XY+Fader oben
    push::TextTile releaseAllButton { "Release All", push::colours::ledRed };
    push::TextTile octaveUpTile   { "Oct +" };
    push::TextTile octaveDownTile { "Oct -" };
    ExpressionRibbon atOffsetRibbon      { "Pressure", true };  // bipolar
    ExpressionRibbon slideOffsetRibbon   { "Slide", true };     // bipolar
    ExpressionRibbon pitchOffsetRibbon   { "Pitch", true };     // bipolar
    ExpressionRibbon modwheelRibbon      { "Mod", false };      // unipolar, Block D1 (an/aus)
    GridKeyboardComponent keyboard;
    grid::CcControlModel ccModel;     // CC-Baukasten (Grid-Page v2)
    CcControlLayer ccLayer;           // Overlay ÜBER dem Keyboard (nach keyboard deklariert)
    grid::CcControlModel systemCcModel;  // System-Controls des XY+Fader-Modus (User 10.07.2026)
    CcControlLayer systemLayer;          // ÜBER dem ccLayer, IMMER Play-Modus (kein Werkzeug)
    grid::ChordMemory chordMemory;    // Akkord-Speicher (Grid-Page v2, 8 LCD-Slots)
    ChordMemoryStrip chordStrip { chordMemory };   // liegt räumlich NEBEN dem Keyboard/ccLayer
    EditorDockPanel dockPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridPage)
};

} // namespace conduit
