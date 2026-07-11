#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GridPanelSettings.h"
#include "Core/GridVoiceEngine.h"
#include "Core/MidiControlInput.h"
#include "Core/MidiDeviceTarget.h"
#include "Core/MidiNoteInput.h"
#include "Core/MpeEncoder.h"
#include "Core/PadGridLayout.h"
#include "LockToggle.h"
#include "NumberFieldBracket.h"
#include "PushTiles.h"
#include "Util/ScaleQuantizer.h"

namespace conduit
{

//==============================================================================
/**
    Tab-Inhalt „Settings" des EditorDockPanels der Grid-Page (Block D1,
    Masterplan Grid/MPE-Ausbau). Selbständig -- bindet direkt an rootState
    (Session-Skala, CLAUDE.md 5.3) und an GridPanelSettings (Persistenz der
    Layout-Werte); Achsen-/Keyboard-seitige Wirkung meldet die View über
    Callbacks an die GridPage, exakt wie MpeShapingView/CcPanel.

    Inhalt (von oben):
      - Performance-Slide-Out: MIDI-Ausgangsport (früher GridPage-Top-Row)
        + Session-Skala-Kacheln (Grundton/Typ, zykeln wie zuvor).
      - Pitch: In-Tune Location (Pad/Finger, Block B1) + In-Tune Width in
        Pad-Prozent (Block B2, NumberFieldBracket).
      - Expression: MPE / Poly-Aftertouch / Mono-Aftertouch (Block B4).
      - Layout: Edit-Grid-Ersatz -- XY-Zeilen (systemLayer-Höhe im
        XY+Fader-Modus) und Fader-Breite (Pitch/Pressure/Slide-Ribbons) als
        NumberFieldBracket statt freier Drag-Resize-Fläche.
        TODO(design): eigentliche Drag-Select-Interaktion aus der
        Roadmap-Beschreibung ("Bereiche anwählbar/größenveränderbar")
        folgt separat, sobald das visuelle Konzept steht -- die Werte sind
        bereits voll wirksam und persistent.
      - Modwheel: Fader neben dem Pitch-Ribbon an/aus (sendet CC1 auf dem
        MPE-Master-Kanal direkt über den MidiDeviceTarget, kein eigener
        Sink-Pfad). MIDI-Kanal-/CC-Zuweisung für die Performance-Controls
        (Port/Kanal/CC, Empfang) bleibt TODO(design)/Block G -- es gibt
        aktuell keine MPE-Kanal-Wahl-Infrastruktur (memberChannelBase ist
        MpeEncoder-Config-fix).

    Message Thread.
*/
class GridSettingsView final : public juce::Component,
                               private juce::ValueTree::Listener
{
public:
    GridSettingsView (juce::ValueTree rootStateToUse, grid::MidiDeviceTarget& midiTargetToUse,
                      grid::MidiControlInput& midiControlInputToUse,
                      grid::MidiNoteInput& noteEchoInputToUse,
                      GridPanelSettings& panelSettingsToUse,
                      grid::InTuneLocation initialInTuneLocation,
                      float initialInTuneWidthPercent,
                      grid::ExpressionMode initialExpressionMode);
    ~GridSettingsView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Ableton-Routing-Optionen (Block H v2): Namen aus der tracks-Domain
        (`input_options`) — die GridPage füttert sie bei Domain-Änderungen.
        Befüllt BEIDE Combos der Sektion „Ableton — Don't-Follow-Routing":
        MIDI Master (follows selection, GridPanelSettings::
        masterMidiInputName) und Grid MPE Port (independent,
        gridMidiInputName — User-Feldtest 11.07.2026: der Conduit-MIDI-Out-
        Portname kann vom Ableton-Routing-Namen abweichen). */
    void setMasterInputOptions (const juce::StringArray& options);

    /** In-Tune Location (Block B1) geändert -- Besitzer reicht an
        GridKeyboardComponent::setInTuneLocation durch. */
    std::function<void (grid::InTuneLocation)> onInTuneLocationChanged;
    /** In-Tune Width 0..100 (Block B2) -- an
        GridKeyboardComponent::setInTuneWidthPercent durchreichen. */
    std::function<void (float)> onInTuneWidthChanged;
    /** Expression Mode (Block B4) -- an MpeMidiSink::setExpressionMode
        durchreichen. */
    std::function<void (grid::ExpressionMode)> onExpressionModeChanged;
    /** systemControlRows ODER ribbonWidthPx haben sich geändert (beide
        bereits in GridPanelSettings persistiert) -- Besitzer ruft
        resized()/applyLayoutMode() erneut auf. */
    std::function<void()> onLayoutSettingsChanged;
    /** Modwheel-Fader an/aus (bereits in GridPanelSettings persistiert). */
    std::function<void (bool)> onModwheelToggled;

    /** Track-Tabs-Position oben/unten geändert (persistiert) — Besitzer
        relayoutet (Block H3 Runde 3). */
    std::function<void()> onTrackTabsChanged;

    /** Root-Pad-Farbmodus umgeschaltet (persistiert, Block I) — Besitzer
        aktualisiert die Keyboard-Färbung (refreshTrackFocus). */
    std::function<void()> onRootColourToggled;

private:
    void valueTreePropertyChanged (juce::ValueTree& tree,
                                   const juce::Identifier& property) override;
    void refreshScaleLabels();
    void rebuildDeviceList();
    void handleDeviceSelected();
    void rebuildInputDeviceList();
    void handleInputDeviceSelected();
    void rebuildEchoDeviceList();
    void handleEchoDeviceSelected();
    void handleMasterInputSelected();
    void handleGridInputSelected();
    void repopulateRoutingCombo (juce::ComboBox& combo, const juce::String& savedName);
    [[nodiscard]] juce::String routingNameForSelection (const juce::ComboBox& combo) const;

    // Section-Ueberschriften: Bounds werden in resized() EINMAL berechnet
    // und in paint() gelesen -- verhindert Auseinanderlaufen zwischen
    // Layout- und Zeichen-Code (Muster MpeShapingView::AxisSection).
    juce::Rectangle<int> pitchHeadingBounds, expressionHeadingBounds,
                         layoutHeadingBounds, modwheelHeadingBounds,
                         abletonHeadingBounds, trackTabsHeadingBounds;

    juce::ValueTree rootState;
    grid::MidiDeviceTarget& midiTarget;
    grid::MidiControlInput& midiControlInput;
    grid::MidiNoteInput& noteEchoInput;
    GridPanelSettings& panelSettings;
    juce::Array<juce::MidiDeviceInfo> devices;
    juce::Array<juce::MidiDeviceInfo> inputDevices;
    juce::Array<juce::MidiDeviceInfo> echoDevices;

    // Performance-Slide-Out (umgezogen aus der ehemaligen GridPage-Top-Row);
    // inputCombo = MIDI-EINGANG fuer die Control-Steuerung (Block G).
    juce::ComboBox outputCombo;
    juce::ComboBox inputCombo;
    juce::ComboBox echoCombo;   // Block H4: Noten-Echo-Eingang (Conduit DAW)
    push::TextTile rootTile  { "C" };
    push::TextTile scaleTile { "Chromatic" };

    // Pitch: In-Tune Location + Width.
    push::TextTile inTuneLocationPadTile    { "Pad" };
    push::TextTile inTuneLocationFingerTile { "Finger" };
    NumberFieldBracket inTuneWidthField;

    // Expression Mode (Block B4).
    push::TextTile expressionMpeTile  { "MPE" };
    push::TextTile expressionPolyTile { "Poly AT" };
    push::TextTile expressionMonoTile { "Mono AT" };

    // Layout (Edit-Grid-Ersatz, Block D1).
    NumberFieldBracket systemRowsField;
    NumberFieldBracket ribbonWidthField;

    // Modwheel.
    LockToggle  modwheelToggle;
    juce::Label modwheelLabel { {}, "Modwheel-Fader" };

    // Ableton (Block H v2): Free-From-Selection-Routing — Master-MIDI-Input
    // (folgt der Selektion) + Grid-MPE-Port (unabhängig davon). Das „+"
    // pflegt die Master-Favoriten des Quick-Switch (Block H3, PopupMenu
    // async aus den input_options, Häkchen = Favorit).
    juce::ComboBox masterInputCombo;
    juce::ComboBox gridInputCombo;
    juce::Label masterInputLabel { {}, "MIDI Master (follows selection)" };
    juce::Label gridInputLabel   { {}, "Grid MPE Port (independent from selection)" };
    push::TextTile masterFavouritesTile { "+" };
    juce::StringArray masterInputOptions;

    // Track Select (Block H3 Runde 3): Position + Schriftgröße der Tabs.
    push::TextTile trackTabsTopTile    { "Top" };
    push::TextTile trackTabsBottomTile { "Bottom" };
    NumberFieldBracket trackTabsFontField;

    // Block I: Root-Pads in Track-Farbe (wie Push).
    LockToggle  rootColourToggle;
    juce::Label rootColourLabel { {}, "Root-Pads in Track-Farbe" };

    void showMasterFavouritesMenu();

public:
    /** Master-Favoriten geändert (Block H3) — GridPage aktualisiert den
        Quick-Switch. */
    std::function<void()> onMasterFavouritesChanged;

private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridSettingsView)
};

} // namespace conduit
