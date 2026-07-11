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

    //==========================================================================
    // Block J (Physics, Masterplan): Grid-Gravity (J1) + Fader/XY-Physics
    // (J3). Force/Mass/Inertia sind der GEMEINSAME Feder-Parametersatz
    // beider Features (grid::SpringParams); Delay/Threshold/Fade sind
    // Gravity-spezifisch. Toggles im Settings-Tab, Tuning im Dev-Panel
    // (live gepollt, Muster TrackTabsStrip).

    static constexpr double defaultPhysicsForce = 400.0;
    static constexpr double minPhysicsForce     = 10.0;
    static constexpr double maxPhysicsForce     = 3000.0;

    static constexpr double defaultPhysicsMass = 1.0;
    static constexpr double minPhysicsMass     = 0.1;
    static constexpr double maxPhysicsMass     = 10.0;

    static constexpr int defaultPhysicsInertia = 40;   // 0..100 %
    static constexpr int minPhysicsInertia     = 0;
    static constexpr int maxPhysicsInertia     = 100;

    static constexpr int defaultGravityDelayMs = 150;
    static constexpr int minGravityDelayMs     = 0;
    static constexpr int maxGravityDelayMs     = 1000;

    static constexpr double defaultGravityThreshold = 0.5;   // Pad-Breiten/s
    static constexpr double minGravityThreshold     = 0.05;
    static constexpr double maxGravityThreshold     = 10.0;

    static constexpr int defaultGravityFadeMs = 250;
    static constexpr int minGravityFadeMs     = 0;
    static constexpr int maxGravityFadeMs     = 2000;

    /** Grid-Gravity global an/aus (J1) — der Pad-Magnet zieht liegende
        Finger auf den perfekten Pitch (Feder mit Überschwingen). */
    [[nodiscard]] bool isGridGravityEnabled() const noexcept { return gridGravityEnabled; }
    void setGridGravityEnabled (bool shouldBeEnabled);

    /** Federkonstante des Physics-Kerns (gemeinsam J1/J3). */
    [[nodiscard]] double getPhysicsForce() const noexcept { return physicsForce; }
    void setPhysicsForce (double newForce);

    /** Träge Masse des Physics-Kerns (gemeinsam J1/J3). */
    [[nodiscard]] double getPhysicsMass() const noexcept { return physicsMass; }
    void setPhysicsMass (double newMass);

    /** Nachschwing-Anteil 0..100 (0 = kriecht, 100 = federt lange). */
    [[nodiscard]] int getPhysicsInertia() const noexcept { return physicsInertia; }
    void setPhysicsInertia (int newInertia);

    /** Stillstands-Verzögerung, bis der Pad-Magnet greift (J1). */
    [[nodiscard]] int getGravityDelayMs() const noexcept { return gravityDelayMs; }
    void setGravityDelayMs (int newDelayMs);

    /** Bewegungsschwelle in Pad-Breiten/s — darüber lässt der Magnet los. */
    [[nodiscard]] double getGravityThreshold() const noexcept { return gravityThreshold; }
    void setGravityThreshold (double newThreshold);

    /** Kraft-Einblendzeit des Magneten (fade-time-force, J1). */
    [[nodiscard]] int getGravityFadeMs() const noexcept { return gravityFadeMs; }
    void setGravityFadeMs (int newFadeMs);

    /** Fader/XY-Physics an/aus (J3): Control-Werte folgen dem Finger über
        die Feder (zweifarbige Anzeige: Ziel cyan, Ist weiß). */
    [[nodiscard]] bool isControlPhysicsEnabled() const noexcept { return controlPhysicsEnabled; }
    void setControlPhysicsEnabled (bool shouldBeEnabled);

    /** Snap-to-Default (J3): Loslassen federt Fader/XY auf den Default-Wert
        zurück (nur wirksam mit Fader/XY-Physics). */
    [[nodiscard]] bool isControlSnapToDefault() const noexcept { return controlSnapToDefault; }
    void setControlSnapToDefault (bool shouldSnap);

    //==========================================================================
    // Block K (Persistenz gebündelt): die bisher Laufzeit-only-Skalare der
    // Blöcke A/B/D — Sensitivity/Bend-Range (MpeShapingView-Detailspalte),
    // In-Tune/Expression (Settings-Tab), Oktav-Shift (Grid-Page-Buttons).
    // Strukturierter Session-Zustand (Bindings/Chords/Kurven) liegt separat
    // im GridSessionStore (GridSession.xml).

    static constexpr double defaultSensitivity = 50.0;   // 0..100, 50 = neutral

    static constexpr int defaultBendRangeIndex = 2;      // ×1 (kMultipliers-Index)
    static constexpr int minBendRangeIndex     = 0;
    static constexpr int maxBendRangeIndex     = 5;

    static constexpr double defaultInTuneWidthPercent = 20.0;   // Block B2

    static constexpr int maxOctaveShift = 3;   // == GridKeyboardComponent::kMaxOctaveShift

    [[nodiscard]] double getPressureSensitivity() const noexcept { return pressureSensitivity; }
    void setPressureSensitivity (double newSensitivity);

    [[nodiscard]] double getSlideSensitivity() const noexcept { return slideSensitivity; }
    void setSlideSensitivity (double newSensitivity);

    /** Index in BendRangeSelector::kMultipliers (¼ ½ ×1 ×2 ×4 ×8). */
    [[nodiscard]] int getBendRangeIndex() const noexcept { return bendRangeIndex; }
    void setBendRangeIndex (int newIndex);

    /** In-Tune-Anker (Block B1): true = Pad-Zentrum (Default), false = Finger. */
    [[nodiscard]] bool isInTuneLocationPad() const noexcept { return inTuneLocationPad; }
    void setInTuneLocationPad (bool shouldBePad);

    [[nodiscard]] double getInTuneWidthPercent() const noexcept { return inTuneWidthPercent; }
    void setInTuneWidthPercent (double newPercent);

    /** ExpressionMode als int (mpe 0, polyAftertouch 1, monoAftertouch 2 —
        Enum-Reihenfolge ist Serialisierungs-API). */
    [[nodiscard]] int getExpressionModeIndex() const noexcept { return expressionModeIndex; }
    void setExpressionModeIndex (int newIndex);

    [[nodiscard]] int getOctaveShift() const noexcept { return octaveShift; }
    void setOctaveShift (int newShift);

    /** Ablage der strukturierten Grid-Session (GridSessionStore, Block K):
        GridSession.xml NEBEN der Settings-Datei — folgt damit automatisch
        den injizierten Test-Options (Temp-Verzeichnis). */
    [[nodiscard]] juce::File sessionFile();

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

    double pressureSensitivity  = defaultSensitivity;
    double slideSensitivity     = defaultSensitivity;
    int    bendRangeIndex       = defaultBendRangeIndex;
    bool   inTuneLocationPad    = true;
    double inTuneWidthPercent   = defaultInTuneWidthPercent;
    int    expressionModeIndex  = 0;
    int    octaveShift          = 0;

    bool   gridGravityEnabled    = false;
    double physicsForce          = defaultPhysicsForce;
    double physicsMass           = defaultPhysicsMass;
    int    physicsInertia        = defaultPhysicsInertia;
    int    gravityDelayMs        = defaultGravityDelayMs;
    double gravityThreshold      = defaultGravityThreshold;
    int    gravityFadeMs         = defaultGravityFadeMs;
    bool   controlPhysicsEnabled = false;
    bool   controlSnapToDefault  = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridPanelSettings)
};

} // namespace conduit
