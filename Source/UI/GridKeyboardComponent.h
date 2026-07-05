#pragma once

#include <map>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GridVoiceEngine.h"
#include "Core/PadGridLayout.h"

namespace conduit
{

//==============================================================================
/**
    Touch-Fläche des Grid-Voice-Modells (M1 Teil 3, CLAUDE.md 14 ADR
    Grid-Page): isomorphes Pad-Raster (PadGridLayout), Multi-Touch über die
    Standard-Maus-/Touch-Callbacks — jeder JUCE-Touch-Source liefert ein
    eigenes event.source.

    Erster Ton, bewusst reduziert (Scope M1 Teil 3): nur Note + Pitch-Bend
    über X + EINE Ausdrucksachse über Y (Pressure). Kein zweiter Finger,
    kein Slide, keine Ribbons/Drone/Latch — das sind eigene Meilensteine.

    Hält keinen eigenen Zustand außer der Per-Finger-Zuordnung; ruft die
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

private:
    struct FingerState
    {
        float startNormX = 0.0f;
        int   padIndex    = -1;
    };

    [[nodiscard]] juce::Point<float> normalisedPosition (const juce::MouseEvent& event) const noexcept;
    [[nodiscard]] static int fingerIdFor (const juce::MouseEvent& event) noexcept;

    grid::GridVoiceEngine& engine;
    grid::PadGridLayout    layout;

    std::map<int, FingerState> fingers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridKeyboardComponent)
};

} // namespace conduit
