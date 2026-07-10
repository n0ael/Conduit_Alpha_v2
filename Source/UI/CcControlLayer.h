#pragma once

#include <functional>
#include <map>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/CcControlModel.h"

namespace conduit
{

//==============================================================================
/**
    Overlay des CC-Baukastens exakt über dem Pad-Raster der Grid-Page
    (Grid-Page v2, Design-Mock): zeichnet und bedient die platzierten
    CC-Controls (CcControlModel) komplett im Layer — keine Kind-Components,
    Zellgeometrie aus cols/rows der PadGridLayout-Konfiguration
    (Zelle (c,r) → c·w/cols, r·h/rows; Control-Fläche = Zell-Union,
    reduced(1)).

    CC-Modus (Dock-Panel offen + Tab „CC", setCcMode): der Layer fängt ALLE
    Events über dem Raster ab — Drag mit gewähltem Werkzeug zieht ein neues
    Control auf (gestrichelte Platzierungs-Vorschau), Drag auf einem Control
    verschiebt grid-snapped (moveTo klemmt in die Grenzen), die ×-Zone oben
    rechts entfernt. Ohne Werkzeug + freie Fläche: Events werden geschluckt
    (keine Noten im CC-Modus, wie im Mock).

    MPE-Modus (Spielen): hitTest lässt freie Flächen zum darunterliegenden
    Keyboard durch (Pads UNTER Controls bleiben stumm), Controls werden
    bedient — Fader (vertikaler Drag, unten 0), Push (an solange gehalten),
    Toggle (Tap), XY (Drag, geklemmt). Multi-Touch: pro MouseInputSource
    das gegriffene Control (JUCE liefert Touch als per-Source-Mouse-Events;
    ein setAcceptsTouchEvents braucht juce::Component nicht). Message Thread.
*/
class CcControlLayer final : public juce::Component
{
public:
    CcControlLayer (grid::CcControlModel& modelToUse, int colsToUse, int rowsToUse);

    /** CC-Modus (Bearbeiten) an/aus — bricht laufende Gesten ab und löst
        gehaltene Push-Controls. */
    void setCcMode (bool shouldEdit);
    [[nodiscard]] bool isCcMode() const noexcept { return ccMode; }

    void setActiveTool (grid::CcTool tool);
    [[nodiscard]] grid::CcTool getActiveTool() const noexcept { return activeTool; }

    /** TODO(design): hier dockt später der MIDI-CC-Versand an — feuert bei
        jeder Wertänderung eines Controls (Fader/Push/Toggle/XY); der Versand
        selbst ist NICHT Teil dieses Meilensteins. */
    std::function<void (const grid::CcControl&)> onControlValueChanged;

    /** MPE-Modus: nur Control-Flächen sind Ziel — freie Flächen fallen zum
        Keyboard durch. CC-Modus: alles wird abgefangen. */
    bool hitTest (int x, int y) override;

    void paint (juce::Graphics& g) override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp   (const juce::MouseEvent& event) override;

private:
    struct Cell { int c = 0; int r = 0; };

    [[nodiscard]] Cell cellAt (juce::Point<float> position) const noexcept;   // geklemmt ins Raster
    [[nodiscard]] juce::Rectangle<float> rectForCells (int c0, int r0, int c1, int r1) const noexcept;
    [[nodiscard]] juce::Rectangle<float> rectFor (const grid::CcControl& control) const noexcept;
    [[nodiscard]] static juce::Rectangle<float> removeZoneFor (juce::Rectangle<float> controlRect) noexcept;

    void drawControl (juce::Graphics& g, const grid::CcControl& control) const;
    void notifyValueChanged (const grid::CcControl& control);

    void handleEditDown (const juce::MouseEvent& event);
    void handleEditDrag (const juce::MouseEvent& event);
    void handleEditUp   (const juce::MouseEvent& event);
    void handlePlayDown (const juce::MouseEvent& event);
    void handlePlayDrag (const juce::MouseEvent& event);
    void handlePlayUp   (const juce::MouseEvent& event);

    /** Wendet die Spiel-Geste auf das Control an (Fader-value, Push/Toggle-on,
        XY-x/y) und meldet onControlValueChanged. */
    void applyPlayGesture (grid::CcControl& control, juce::Point<float> position, bool isDown);

    grid::CcControlModel& model;
    const int cols;
    const int rows;

    bool ccMode = false;
    grid::CcTool activeTool = grid::CcTool::none;

    // Bearbeiten (CC-Modus): EIN Edit-Vorgang zur Zeit — weitere Finger
    // werden während eines laufenden Platzier-/Verschiebe-Drags ignoriert
    // (konservativ; Multi-Touch-Edit ist nicht Teil des Mocks).
    bool placing = false;
    Cell placeStart, placeCurrent;
    int  movingId   = -1;
    Cell moveGrabOffset;
    int  editFinger = -1;

    // Spielen (MPE-Modus), Multi-Touch: MouseInputSource-Index → Control-Id.
    std::map<int, int> grabbedControls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CcControlLayer)
};

} // namespace conduit
