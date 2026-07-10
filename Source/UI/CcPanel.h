#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/CcControlModel.h"

namespace conduit
{

//==============================================================================
/**
    Tab-Inhalt „CC" des EditorDockPanels der Grid-Page (Grid-Page v2,
    Design-Mock): Hinweistext + 2×2-Werkzeugraster (Fader/Push/Toggle/XY).
    Tap wählt ein Werkzeug, Tap auf das aktive Werkzeug deselektiert
    (CcTool::none) — onToolChanged feuert bei jeder Änderung.

    Die Kacheln werden direkt gezeichnet und hit-getestet (keine
    Kind-Components); Icons vektorbasiert nach PushIcons-Konvention
    (normierte Geometrie in Ziel-Bounds skaliert, keine Bitmaps).
    Touch läuft über die Standard-Maus-Callbacks (JUCE liefert pro
    Touch-Source eigene Events). Message Thread.
*/
class CcPanel final : public juce::Component
{
public:
    CcPanel();

    [[nodiscard]] grid::CcTool getActiveTool() const noexcept { return activeTool; }

    /** Feuert bei jeder Werkzeug-Änderung (auch Deselektieren → none). */
    std::function<void (grid::CcTool)> onToolChanged;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;

private:
    struct ToolTile
    {
        grid::CcTool tool = grid::CcTool::none;
        juce::String label;
        juce::Rectangle<int> bounds;
    };

    /** Werkzeug-Icon (~22 px) als Vektorgeometrie in die iconBounds. */
    static void drawToolIcon (juce::Graphics& g, grid::CcTool tool,
                              juce::Rectangle<float> iconBounds);

    [[nodiscard]] int tileIndexAt (juce::Point<int> position) const noexcept;

    std::array<ToolTile, 4> tiles;
    grid::CcTool activeTool = grid::CcTool::none;
    int hoveredTile = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CcPanel)
};

} // namespace conduit
