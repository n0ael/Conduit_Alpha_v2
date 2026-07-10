#pragma once

#include <map>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GridVoiceEngine.h"
#include "Core/PadGridLayout.h"
#include "Core/RingTouchModel.h"
#include "Util/ScaleQuantizer.h"

namespace conduit
{

//==============================================================================
/**
    Touch-Fläche des Grid-Voice-Modells (M1 Teil 3, CLAUDE.md 14 ADR
    Grid-Page): isomorphes Pad-Raster (PadGridLayout), Multi-Touch über die
    Standard-Maus-/Touch-Callbacks — jeder JUCE-Touch-Source liefert ein
    eigenes event.source.

    Note + Pitch-Bend über X + Pressure über Y (M1 Teil 3) plus Ring-
    Mechanik als zweite Ausdrucksachse (M1b-2, "Sonne/Mond/Orbit"): ein
    zweiter Finger im Greifband eines liegenden primären Fingers (Sonne)
    wird dessen Ring-Finger (Mond), sein Abstand zur Sonne (Orbit-Radius)
    moduliert setSlide des primären Fingers (RingTouchModel).
    Bewusst weiterhin ohne Drone/Latch, Pinch, Doppeltipp, Drift-über-Rand,
    Rand-Ribbons, Release-All — eigene Meilensteine.

    Hält keinen eigenen Zustand außer der Per-Finger-Zuordnung (primär),
    der Session-Skala (setScale) und dem RingTouchModel; ruft die
    GridVoiceEngine& direkt (Message Thread, CLAUDE.md 4.2 ITouchMacro).
*/
class GridKeyboardComponent final : public juce::Component
{
public:
    explicit GridKeyboardComponent (grid::GridVoiceEngine& engineToUse,
                                     const grid::PadGridLayout::Config& layoutConfig = {});

    void paint (juce::Graphics& g) override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp   (const juce::MouseEvent& event) override;

    /** Session-Skala (Root-Tree scaleRoot/scaleType, Schema 6.2) — färbt die
        Pad-Grundfarben (Grundton/Skalenton/skalenfremd). Message Thread. */
    void setScale (int newRootNote, ScaleType newScaleType);

    /** Pad-Grundfarbe nach Session-Skala (Design-Mock Grid-Page v2):
        Grundton padRoot, Skalenton tile, skalenfremd padUnlit — pure
        function, testbar ohne Component. */
    [[nodiscard]] static juce::Colour padBaseColour (int midiNote, int rootNote,
                                                     ScaleType type) noexcept;

private:
    struct FingerState
    {
        float startNormX = 0.0f;
        float startNormY = 0.0f;
    };

    [[nodiscard]] juce::Point<float> normalisedPosition (const juce::MouseEvent& event) const noexcept;
    [[nodiscard]] static int fingerIdFor (const juce::MouseEvent& event) noexcept;

    grid::GridVoiceEngine& engine;
    grid::PadGridLayout    layout;
    grid::RingTouchModel   ring;

    int       scaleRootNote = 0;
    ScaleType sessionScale  = ScaleType::chromatic;

    std::map<int, FingerState> fingers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridKeyboardComponent)
};

} // namespace conduit
