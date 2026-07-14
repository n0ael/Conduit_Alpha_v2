#pragma once

#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Modules/ChassisSchema.h"
#include "PushLookAndFeel.h"

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
    /** Macro-Modulations-Marker (MIDI-Rig M5c): zweiter Marker am
        EFFEKTIVWERT (Basis + Modulation, GraphManager-Bus) plus
        Verbindungslinie zum Griff (Basis) — reine Anzeige, der
        Slider-WERT bleibt die Basis. nullopt blendet den Marker aus.
        Gefüttert vom 30-Hz-Meter-Tick des FxModulePanel. */
    void setModulationValue (std::optional<double> newValue)
    {
        const auto changed = modulationValue.has_value() != newValue.has_value()
                             || (newValue.has_value()
                                 && ! juce::exactlyEqual (*modulationValue, *newValue));
        if (! changed)
            return;

        modulationValue = newValue;
        repaint();
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (! modulationValue.has_value())
            return;

        // getPositionOfValue liefert die Pixel-Position entlang der
        // Laufbahn (inkl. Response-Kurve via valueToProportionOfLength).
        const auto modPos  = getPositionOfValue (*modulationValue);
        const auto basePos = getPositionOfValue (getValue());

        if (isVertical())
        {
            const auto x = (float) getWidth() * 0.5f;
            g.setColour (push::colours::ledCyan.withAlpha (0.5f));
            g.drawLine (x, basePos, x, modPos, 2.0f);
            g.setColour (push::colours::ledCyan);
            g.fillRoundedRectangle (juce::Rectangle<float> ((float) getWidth() - 8.0f, 3.0f)
                                        .withCentre ({ x, modPos }),
                                    1.5f);
        }
        else
        {
            const auto y = (float) getHeight() * 0.5f;
            g.setColour (push::colours::ledCyan.withAlpha (0.5f));
            g.drawLine (basePos, y, modPos, y, 2.0f);
            g.setColour (push::colours::ledCyan);
            g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, (float) getHeight() - 8.0f)
                                        .withCentre ({ modPos, y }),
                                    1.5f);
        }
    }

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
    std::optional<double> modulationValue;   // M5c: Effektivwert (Anzeige)
};

} // namespace conduit
