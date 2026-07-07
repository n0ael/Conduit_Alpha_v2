#pragma once

#include <juce_data_structures/juce_data_structures.h>

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

    /** Eigene Datei neben Meter.settings / Ui.settings. */
    [[nodiscard]] static juce::PropertiesFile::Options defaultOptions();

    /** Tests injizieren eigene Options (Temp-Verzeichnis). */
    explicit GridPanelSettings (const juce::PropertiesFile::Options& options = defaultOptions());
    ~GridPanelSettings();

    [[nodiscard]] int getEditorPanelWidth() const noexcept { return editorPanelWidth; }
    void setEditorPanelWidth (int newWidth);

    [[nodiscard]] bool isEditorPanelOpen() const noexcept { return editorPanelOpen; }
    void setEditorPanelOpen (bool shouldBeOpen);

private:
    void loadFromFile();

    juce::ApplicationProperties applicationProperties;
    int  editorPanelWidth = defaultWidth;
    bool editorPanelOpen  = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridPanelSettings)
};

} // namespace conduit
