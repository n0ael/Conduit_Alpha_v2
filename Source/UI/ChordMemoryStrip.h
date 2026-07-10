#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/ChordMemory.h"

namespace conduit
{

//==============================================================================
/**
    Akkord-Speicher-Leiste der Grid-Page (Grid-Page v2, Feature 6,
    Design-Mock): 8 vertikale "LCD-Screens" zwischen Pad-Raster und rechter
    Ribbon-Spalte. Slots von oben (1) nach unten (8), Gap 2 px, Außen-
    Padding 1 px (Pad-Raster-Optik); pro Slot rein schwarze Fläche
    (push::colours::lcdScreen, KEIN Verlauf), 1-px-Kontur, Radius 4,
    Slot-Nummer klein unten rechts.

    Mini-Ansicht belegter Slots (Mock renderVals maßgeblich): Sonne =
    gefüllter 6-px-Kreis ledWhite bei (x·slotW, y·slotH); bei hasOrbit
    zusätzlich Mond (4 px) bei ((x+ox)·slotW, (y+oy·aspect)·slotH) und
    Orbit-ELLIPSE (1 px, ledWhite Alpha 0.55) mit x-Radius r·slotW und
    y-Radius r·slotW·aspect (r = hypot(ox, oy), aspect = Spielflächen-
    Breite/Höhe via setSurfaceAspect). Punkte dürfen über den Slot-Rand —
    geclippt auf die Slot-Fläche.

    Interaktion (MPE-Modus): Tap auf leeren Slot speichert die aktuelle
    Konstellation (getConstellation), Tap auf belegten Slot ruft sie ab
    (onRecall) und startet eine Drag-Session — Ziehen verschiebt den
    Akkord starr (onMoveBy, Pixel-Deltas; Strip und Keyboard haben
    denselben Screen-Maßstab), Loslassen stoppt nur das Verschieben.
    CC-Modus (isCcMode): Tap auf belegten Slot löscht ihn. Message Thread.
*/
class ChordMemoryStrip final : public juce::Component
{
public:
    explicit ChordMemoryStrip (grid::ChordMemory& memoryToUse);

    /** Aspekt (Breite/Höhe) der Spielfläche — die Mini-Ansicht rechnet den
        Mond-Offset (beide Komponenten über die Flächen-BREITE normalisiert)
        damit in Slot-Koordinaten um. GridPage setzt ihn in resized(). */
    void setSurfaceAspect (float widthOverHeight) noexcept;

    /** Aktuelle Konstellation des Keyboards (live + latched, normalisiert)
        — leer = nichts zu speichern. */
    std::function<std::vector<grid::StoredSun>()> getConstellation;

    /** Belegter Slot angetippt (MPE-Modus): Konstellation abrufen. */
    std::function<void (int slot)> onRecall;

    /** Drag in der Recall-Session: latched Konstellation starr verschieben. */
    std::function<void (float dxPx, float dyPx)> onMoveBy;

    /** CC-Modus (Bearbeiten) aktiv? — Tap auf belegten Slot löscht dann. */
    std::function<bool()> isCcMode;

    void paint (juce::Graphics& g) override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp   (const juce::MouseEvent& event) override;

private:
    static constexpr int kGapPx     = 2;
    static constexpr int kPaddingPx = 1;

    [[nodiscard]] juce::Rectangle<float> slotBounds (int slot) const noexcept;
    [[nodiscard]] int slotAt (juce::Point<float> position) const noexcept;

    grid::ChordMemory& memory;
    float surfaceAspect = 2.0f;   // Spielflächen-Breite/Höhe (setSurfaceAspect)

    // Recall-Drag-Session: EIN Drag zur Zeit — weitere Finger werden während
    // einer laufenden Session ignoriert (multi-touch-tolerant, konservativ).
    int dragSourceIndex = -1;
    juce::Point<float> lastDragPosition;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordMemoryStrip)
};

} // namespace conduit
