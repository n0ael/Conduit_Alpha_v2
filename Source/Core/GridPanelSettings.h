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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridPanelSettings)
};

} // namespace conduit
