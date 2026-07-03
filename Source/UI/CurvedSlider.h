#pragma once

#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Modules/ChassisSchema.h"

namespace conduit
{

//==============================================================================
/**
    Slider mit Bezier-Response-Kurve (Dev-Modus 4.6): mappt die Griff-
    Position über die Kurve in den Wertebereich — REINES UI-Mapping, der
    Slider-WERT (und damit Tree/OSC/DSP) bleibt der echte Wert. Ohne Kurve
    verhält er sich exakt wie juce::Slider (linear).

    Die Kurven-Monotonie garantiert ChassisSchema::parseCurve (Kontroll-
    punkte auf [0,1] geclamped) — das Mapping ist eindeutig invertierbar.
*/
class CurvedSlider final : public juce::Slider
{
public:
    using juce::Slider::Slider;

    void setResponseCurve (std::optional<ChassisSchema::BezierCurve> curveToUse)
    {
        curve = curveToUse;
        repaint();
    }

    [[nodiscard]] bool hasResponseCurve() const noexcept { return curve.has_value(); }

    //==========================================================================
    double proportionOfLengthToValue (double proportion) override
    {
        if (! curve.has_value())
            return juce::Slider::proportionOfLengthToValue (proportion);

        const auto norm = ChassisSchema::evaluateCurve (*curve, static_cast<float> (proportion));
        return getMinimum() + static_cast<double> (norm) * (getMaximum() - getMinimum());
    }

    double valueToProportionOfLength (double value) override
    {
        if (! curve.has_value())
            return juce::Slider::valueToProportionOfLength (value);

        const auto length = getMaximum() - getMinimum();

        if (length <= 0.0)
            return 0.0;

        return static_cast<double> (ChassisSchema::curvePositionForValue (
            *curve, static_cast<float> ((value - getMinimum()) / length)));
    }

private:
    std::optional<ChassisSchema::BezierCurve> curve;
};

} // namespace conduit
