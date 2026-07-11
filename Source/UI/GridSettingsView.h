#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GridPanelSettings.h"
#include "Core/GridVoiceEngine.h"
#include "Core/MidiControlInput.h"
#include "Core/MidiDeviceTarget.h"
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
                      GridPanelSettings& panelSettingsToUse,
                      grid::InTuneLocation initialInTuneLocation,
                      float initialInTuneWidthPercent,
                      grid::ExpressionMode initialExpressionMode);
    ~GridSettingsView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Master-MIDI-Input-Optionen (Block H v2): Ableton-Routing-Namen aus
        der tracks-Domain (`input_options`) — die GridPage füttert sie bei
        Domain-Änderungen. Auswahl persistiert in
        GridPanelSettings::masterMidiInputName (Ziel-Routing der NICHT vom
        Grid gespielten Tracks, z. B. "FromPush"). */
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

private:
    void valueTreePropertyChanged (juce::ValueTree& tree,
                                   const juce::Identifier& property) override;
    void refreshScaleLabels();
    void rebuildDeviceList();
    void handleDeviceSelected();
    void rebuildInputDeviceList();
    void handleInputDeviceSelected();
    void handleMasterInputSelected();

    // Section-Ueberschriften: Bounds werden in resized() EINMAL berechnet
    // und in paint() gelesen -- verhindert Auseinanderlaufen zwischen
    // Layout- und Zeichen-Code (Muster MpeShapingView::AxisSection).
    juce::Rectangle<int> pitchHeadingBounds, expressionHeadingBounds,
                         layoutHeadingBounds, modwheelHeadingBounds,
                         abletonHeadingBounds;

    juce::ValueTree rootState;
    grid::MidiDeviceTarget& midiTarget;
    grid::MidiControlInput& midiControlInput;
    GridPanelSettings& panelSettings;
    juce::Array<juce::MidiDeviceInfo> devices;
    juce::Array<juce::MidiDeviceInfo> inputDevices;

    // Performance-Slide-Out (umgezogen aus der ehemaligen GridPage-Top-Row);
    // inputCombo = MIDI-EINGANG fuer die Control-Steuerung (Block G).
    juce::ComboBox outputCombo;
    juce::ComboBox inputCombo;
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

    // Ableton (Block H v2): Master-MIDI-Input der anderen Tracks.
    juce::ComboBox masterInputCombo;
    juce::StringArray masterInputOptions;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridSettingsView)
};

} // namespace conduit
