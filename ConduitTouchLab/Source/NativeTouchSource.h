#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "TouchSample.h"

namespace touchlab
{

//==============================================================================
/**
    NATIV-Arm: transparente Overlay-Component über der Trace-Fläche. Fängt
    JUCE-MouseEvents (Maus wie Touch) und schiebt sie als TouchSample in den
    Sink. Das ist exakt der Pfad, den Conduits UI heute nutzt — inklusive
    Windows' Koordinaten-Vorverarbeitung (ptPixelLocation, prädiziert).
*/
class NativeTouchSource final : public juce::Component
{
public:
    explicit NativeTouchSource (TouchSink& sinkToUse);

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

private:
    void emit (const juce::MouseEvent& e, Phase phase);

    TouchSink& sink;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NativeTouchSource)
};

} // namespace touchlab
