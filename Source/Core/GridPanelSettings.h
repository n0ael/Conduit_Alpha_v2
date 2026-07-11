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
    // Block H v2 rev5 (Track-Fokus-Routing, statische Aufteilung):
    // Master-MIDI-Input + Grid-MPE-Port.

    /** Ableton-Routing-Name des Master-MIDI-Eingabegeräts (z. B. "FromPush")
        — Auswahl aus den input_options der tracks-Domain (Settings-Tab);
        alle All-Ins-MIDI-Tracks wandern beim Fokus-Setzen statisch dorthin;
        leer = Routing der anderen Tracks unangetastet lassen. */
    [[nodiscard]] juce::String getMasterMidiInputName() const noexcept { return masterMidiInputName; }
    void setMasterMidiInputName (const juce::String& newName);

    /** Ableton-Routing-Name des Grid-MPE-Ports (Input des Fokus-Tracks,
        unabhängig von der Selektion) — User-Feldtest 11.07.2026: explizit
        wählbar statt vom Conduit-MIDI-Out-Portnamen abgeleitet (die können
        sich unterscheiden). Leer = Fallback auf
        MidiDeviceTarget::currentDeviceName(). */
    [[nodiscard]] juce::String getGridMidiInputName() const noexcept { return gridMidiInputName; }
    void setGridMidiInputName (const juce::String& newName);

    /** Master-Device-Favoriten (Block H3): Ableton-Routing-Namen für den
        Quick-Switch oben links auf der Grid-Page (Push ↔ Keyboard …) —
        gepflegt über das „+" im Settings-Tab, persistiert als
        „;"-getrennte Liste. */
    [[nodiscard]] juce::StringArray getMasterMidiFavourites() const { return masterMidiFavourites; }
    void setMasterMidiFavourites (const juce::StringArray& newFavourites);

    //==========================================================================
    // Block H3 Runde 3 (User-Feedback): Track-Tabs-Darstellung.

    static constexpr int defaultTrackTabsFontPx = 12;
    static constexpr int minTrackTabsFontPx     = 9;
    static constexpr int maxTrackTabsFontPx     = 18;

    static constexpr int defaultTrackTabMinWidthPx = 90;
    static constexpr int minTrackTabMinWidthPx     = 40;
    static constexpr int maxTrackTabMinWidthPx     = 280;

    /** Track-Tabs unterhalb der Grid statt oben (Settings-Tab). */
    [[nodiscard]] bool isTrackTabsBottom() const noexcept { return trackTabsBottom; }
    void setTrackTabsBottom (bool shouldBeBottom);

    /** Schriftgröße der Track-Tabs (kleine Displays, Settings-Tab). */
    [[nodiscard]] int getTrackTabsFontPx() const noexcept { return trackTabsFontPx; }
    void setTrackTabsFontPx (int newFontPx);

    /** Mindestbreite eines Track-Tabs (Dev-Panel) — bei großen Projekten
        wird der Strip damit horizontal scrollbar statt unlesbar schmal. */
    [[nodiscard]] int getTrackTabMinWidthPx() const noexcept { return trackTabMinWidthPx; }
    void setTrackTabMinWidthPx (int newWidthPx);

    /** Block I: Root-Pads in der Farbe des Fokus-Tracks hervorheben
        (wie Push; Settings-Toggle) — aus = neutrales padRoot-Grau. */
    [[nodiscard]] bool isRootPadTrackColour() const noexcept { return rootPadTrackColour; }
    void setRootPadTrackColour (bool shouldUseTrackColour);

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

    juce::String masterMidiInputName;
    juce::String gridMidiInputName;
    juce::StringArray masterMidiFavourites;

    bool trackTabsBottom = false;
    int  trackTabsFontPx = defaultTrackTabsFontPx;
    int  trackTabMinWidthPx = defaultTrackTabMinWidthPx;
    bool rootPadTrackColour = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridPanelSettings)
};

} // namespace conduit
