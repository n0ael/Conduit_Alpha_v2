#pragma once

#include <array>

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

#include "GridVoiceEngine.h"

namespace conduit
{

//==============================================================================
/**
    Chrome-Zustand des rechten Editor-Dock-Panels der Grid-Page
    (EditorDockPanel, S2-Vorstufe MPE-Shaping) — App-Zustand, KEIN
    Patch-Zustand (eigene juce::PropertiesFile "Conduit/GridPanel.settings",
    Muster MeterSettings/UiSettings).

    Hält NUR das Panel-Chrome (Breite, offen/zu) — die Tab-Inhalte selbst
    sind reines UI-Gerüst ohne eigenen persistenten Zustand.

    Threading: Setter/Getter auf dem Message Thread.
*/
class GridPanelSettings
{
public:
    static constexpr int defaultWidth = 280;

    // MPE-Shaping-Editor (S2c) -- Dev-Modus-Werte, nur im Dev-Modus sichtbar/
    // einstellbar, aber unabhängig davon immer persistent.
    static constexpr int defaultThresholdWidth = 340;
    static constexpr int minThresholdWidth     = 240;
    static constexpr int maxThresholdWidth     = 600;

    static constexpr int defaultNoteCircleFadeMs = 180;
    static constexpr int minNoteCircleFadeMs     = 0;
    static constexpr int maxNoteCircleFadeMs     = 600;

    /** Eigene Datei neben Meter.settings / Ui.settings. */
    [[nodiscard]] static juce::PropertiesFile::Options defaultOptions();

    /** Tests injizieren eigene Options (Temp-Verzeichnis). */
    explicit GridPanelSettings (const juce::PropertiesFile::Options& options = defaultOptions());
    ~GridPanelSettings();

    [[nodiscard]] int getEditorPanelWidth() const noexcept { return editorPanelWidth; }
    void setEditorPanelWidth (int newWidth);

    [[nodiscard]] bool isEditorPanelOpen() const noexcept { return editorPanelOpen; }
    void setEditorPanelOpen (bool shouldBeOpen);

    /** Breitenschwelle des MPE-Shaping-Editors, ab der die Detailspalte
        erscheint (Dev-Slider, geklemmt [minThresholdWidth,maxThresholdWidth]). */
    [[nodiscard]] int getEditorThresholdWidth() const noexcept { return editorThresholdWidth; }
    void setEditorThresholdWidth (int newWidth);

    /** Fade-Dauer der Live-Noten-Kreise in ms nach dem Loslassen (Dev-Slider,
        geklemmt [minNoteCircleFadeMs,maxNoteCircleFadeMs]). */
    [[nodiscard]] int getNoteCircleFadeMs() const noexcept { return noteCircleFadeMs; }
    void setNoteCircleFadeMs (int newFadeMs);

    /** Farbe einer Grid-Achse (MpeShapingView-Kurve/Label/Noten-Kreise,
        LockToggle-Akzent, ExpressionRibbon-Füllung) — persistiert als
        Hex-String (juce::Colour::toString). Defaults: Pressure = ledOrange,
        Slide = ledCyan, PitchBend = ledGreen (Design-Mock Grid-Page v2). */
    [[nodiscard]] juce::Colour getAxisColour (grid::GridVoiceEngine::Axis axis) const noexcept;
    void setAxisColour (grid::GridVoiceEngine::Axis axis, juce::Colour newColour);

    //==========================================================================
    /** Pad-Layout der Grid-Page (User 10.07.2026): fullPads = alle 64 Pads
        (Push-Style), xyFaders = obere zwei Pad-Reihen als XY-Pad + Fader
        (System-Controls). Persistiert als int (Key "gridLayoutMode"). */
    enum class GridLayoutMode { fullPads = 0, xyFaders = 1 };

    [[nodiscard]] GridLayoutMode getGridLayoutMode() const noexcept { return gridLayoutMode; }
    void setGridLayoutMode (GridLayoutMode newMode);

    //==========================================================================
    // Block D1 (Settings-Tab): strukturelle Layout-Werte, analog zu
    // gridLayoutMode sofort persistent (kein Warten auf Block K -- das
    // bündelt nur die MPE-Achsen-/Kurven-Zustände aus Block B/C).

    static constexpr int defaultSystemControlRows = 2;
    static constexpr int minSystemControlRows     = 1;
    static constexpr int maxSystemControlRows     = 4;

    static constexpr int defaultRibbonWidthPx = 72;
    static constexpr int minRibbonWidthPx     = 40;
    static constexpr int maxRibbonWidthPx     = 140;

    /** Zeilenzahl des systemLayer im XY+Fader-Modus (Edit-Grid, Block D1 --
        Ersatz für die freie Drag-Resize-UI der Roadmap-Beschreibung;
        TODO(design): eigentliche Drag-Interaktion folgt separat). */
    [[nodiscard]] int getSystemControlRows() const noexcept { return systemControlRows; }
    void setSystemControlRows (int newRows);

    /** Breite der drei Performance-Offset-Ribbons (Pitch/Pressure/Slide),
        gemeinsam eingestellt (Block D1 "Fader-Breiten"). */
    [[nodiscard]] int getRibbonWidthPx() const noexcept { return ribbonWidthPx; }
    void setRibbonWidthPx (int newWidthPx);

    /** Modwheel-Fader neben dem Pitch-Ribbon an/aus (Block D1). Sendet CC1
        auf dem MPE-Master-Kanal direkt über den MidiDeviceTarget der
        Grid-Page -- kein eigener Sink-Pfad nötig. */
    [[nodiscard]] bool isModwheelEnabled() const noexcept { return modwheelEnabled; }
    void setModwheelEnabled (bool shouldBeEnabled);

    //==========================================================================
    // Block H v2 (Track-Fokus-Routing): Follow-Selection + Master-MIDI-Input.

    /** Follow Selection (Track-Selector): das Remote Script hält Lives
        Selektion im Hintergrund synchron (neu selektierter All-Ins-Track →
        Master-MIDI-Input + Auto, vorheriger zurück auf All Ins + Off). */
    [[nodiscard]] bool isMidiFollowSelection() const noexcept { return midiFollowSelection; }
    void setMidiFollowSelection (bool shouldFollow);

    /** Ableton-Routing-Name des Master-MIDI-Eingabegeräts (z. B. "FromPush")
        — Auswahl aus den input_options der tracks-Domain (Settings-Tab);
        leer = Routing der anderen Tracks unangetastet lassen. */
    [[nodiscard]] juce::String getMasterMidiInputName() const noexcept { return masterMidiInputName; }
    void setMasterMidiInputName (const juce::String& newName);

private:
    void loadFromFile();

    /** Axis → Index in axisColours (Pressure 0, Slide 1, PitchBend 2). */
    [[nodiscard]] static size_t axisIndex (grid::GridVoiceEngine::Axis axis) noexcept;

    juce::ApplicationProperties applicationProperties;
    int  editorPanelWidth     = defaultWidth;
    bool editorPanelOpen      = false;
    int  editorThresholdWidth = defaultThresholdWidth;
    int  noteCircleFadeMs     = defaultNoteCircleFadeMs;
    GridLayoutMode gridLayoutMode = GridLayoutMode::fullPads;
    std::array<juce::Colour, 3> axisColours {};   // geladen in loadFromFile()

    int  systemControlRows = defaultSystemControlRows;
    int  ribbonWidthPx     = defaultRibbonWidthPx;
    bool modwheelEnabled   = false;

    bool midiFollowSelection = true;
    juce::String masterMidiInputName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridPanelSettings)
};

} // namespace conduit
