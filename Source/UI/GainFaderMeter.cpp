#include "GainFaderMeter.h"

#include <array>

#include "Modules/ProcessorModule.h"
#include "UI/LevelMeterBar.h"   // normFromLinear (dBFS-Mapping)

namespace conduit
{

namespace
{
    constexpr int   sliderColumn  = 26;    // Fader links
    constexpr int   scaleColumn   = 16;    // dB-Zahlen mittig
    constexpr int   clipZoneHeight = 10;   // Clip-Feld über den Meter-Balken
    constexpr float changeEpsilon = 0.002f;

    constexpr std::array<int, 5> scaleMarksDb { 0, -12, -24, -36, -48 };

    juce::Colour levelColour (float norm)
    {
        if (norm > 0.95f) return juce::Colour (0xffe14b3b);
        if (norm > 0.80f) return juce::Colour (0xffd8b13a);
        return juce::Colour (0xff3fb56b);
    }
}

//==============================================================================
GainFaderMeter::GainFaderMeter (juce::ValueTree nodeTreeToBind, juce::String gainParamId,
                                GraphManager& graphManagerToUse, bool useInputMeter)
    : nodeTree (std::move (nodeTreeToBind)),
      paramId (std::move (gainParamId)),
      graphManager (graphManagerToUse),
      nodeUuid (nodeTree.getProperty (id::nodeId).toString()),
      isInputMeter (useInputMeter)
{
    nodeTree.addListener (this);

    const auto param = paramTree();
    slider.setRange ((double) param.getProperty (id::paramMin, -60.0),
                     (double) param.getProperty (id::paramMax, 6.0), 0.0);
    slider.setValue ((double) param.getProperty (id::paramValue, 0.0),
                     juce::dontSendNotification);
    slider.setDoubleClickReturnValue (true, (double) param.getProperty (id::paramDefault, 0.0));

    // Schreibt NUR in den Tree — der GraphManager spiegelt aufs Atomic (6.1);
    // bewusst ohne UndoManager (Muster ParameterPanel).
    slider.onValueChange = [this]
    {
        if (auto p = paramTree(); p.isValid())
            p.setProperty (id::paramValue, slider.getValue(), nullptr);
    };
    addAndMakeVisible (slider);

    startTimerHz (30);  // Meter-Refresh (CLAUDE.md 10)
}

GainFaderMeter::~GainFaderMeter()
{
    nodeTree.removeListener (this);
}

void GainFaderMeter::stopUpdates()
{
    stopTimer();
    nodeTree.removeListener (this);
    setInterceptsMouseClicks (false, false);
    slider.setEnabled (false);
}

//==============================================================================
juce::ValueTree GainFaderMeter::paramTree() const
{
    return nodeTree.getChildWithName (id::parameters)
                   .getChildWithProperty (id::paramId, paramId);
}

const LevelMeter* GainFaderMeter::resolveMeter() const
{
    auto* module = dynamic_cast<ProcessorModule*> (graphManager.getModuleFor (nodeUuid));

    if (module == nullptr)
        return nullptr;  // Deleting/Preset-Load/Tests — leerer Track

    return isInputMeter ? &module->getInputMeter() : &module->getOutputMeter();
}

//==============================================================================
void GainFaderMeter::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (property != id::paramValue || ! tree.hasType (id::parameter)
        || tree.getProperty (id::paramId).toString() != paramId)
        return;

    slider.setValue ((double) tree.getProperty (id::paramValue), juce::dontSendNotification);
}

void GainFaderMeter::timerCallback()
{
    const auto* meter = resolveMeter();

    if (meter == nullptr)
        return;

    bool changed = false;

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto rms  = meter->getRms (channel);
        const auto peak = meter->getPeak (channel);
        const auto hold = meter->getPeakHold (channel);

        changed = changed
               || std::abs (rms - lastRms[channel])   > changeEpsilon
               || std::abs (peak - lastPeak[channel]) > changeEpsilon
               || std::abs (hold - lastHold[channel]) > changeEpsilon;

        lastRms[channel]  = rms;
        lastPeak[channel] = peak;
        lastHold[channel] = hold;
    }

    const auto clip = meter->isClipped (0) || meter->isClipped (1);
    changed = changed || clip != lastClipped;
    lastClipped = clip;

    if (changed)
        repaint();
}

//==============================================================================
juce::Rectangle<float> GainFaderMeter::meterArea() const
{
    return getLocalBounds().toFloat()
        .withTrimmedLeft ((float) (sliderColumn + scaleColumn))
        .withTrimmedTop ((float) clipZoneHeight)
        .reduced (0.0f, 1.0f);
}

juce::Rectangle<float> GainFaderMeter::clipZone() const
{
    return getLocalBounds().toFloat()
        .withTrimmedLeft ((float) (sliderColumn + scaleColumn))
        .withHeight ((float) clipZoneHeight - 2.0f);
}

void GainFaderMeter::resized()
{
    slider.setBounds (getLocalBounds().removeFromLeft (sliderColumn));
}

void GainFaderMeter::paint (juce::Graphics& g)
{
    // dB-Skala zwischen Fader und Meter (Ableton-Optik)
    const auto scaleX = (float) sliderColumn;
    const auto area   = meterArea();

    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.setColour (juce::Colours::white.withAlpha (0.45f));

    for (const auto db : scaleMarksDb)
    {
        const auto norm = ((float) db - LevelMeterBar::minDb) / (0.0f - LevelMeterBar::minDb);
        const auto tickY = area.getBottom() - norm * area.getHeight();
        g.drawText (juce::String (std::abs (db)),
                    juce::Rectangle<float> (scaleX, tickY - 6.0f, (float) scaleColumn - 2.0f, 12.0f),
                    juce::Justification::centredRight);
    }

    // Zwei Meter-Balken (Stereo) nebeneinander
    const auto barWidth = (area.getWidth() - 2.0f) / 2.0f;

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto bar = juce::Rectangle<float> (area.getX() + (float) channel * (barWidth + 2.0f),
                                                 area.getY(), barWidth, area.getHeight());

        g.setColour (juce::Colour (0xff1b1e22));
        g.fillRoundedRectangle (bar, 2.0f);

        const auto usable   = bar.reduced (1.0f);
        const auto rmsNorm  = LevelMeterBar::normFromLinear (lastRms[channel]);
        const auto peakNorm = LevelMeterBar::normFromLinear (lastPeak[channel]);
        const auto holdNorm = LevelMeterBar::normFromLinear (lastHold[channel]);

        if (rmsNorm > 0.0f)
        {
            const auto fillHeight = usable.getHeight() * rmsNorm;
            g.setColour (levelColour (rmsNorm).withAlpha (0.85f));
            g.fillRoundedRectangle (usable.withTop (usable.getBottom() - fillHeight), 2.0f);
        }

        if (peakNorm > 0.0f)
        {
            const auto peakY = usable.getBottom() - usable.getHeight() * peakNorm;
            g.setColour (levelColour (peakNorm));
            g.fillRect (juce::Rectangle<float> (usable.getX(), peakY - 1.0f, usable.getWidth(), 2.0f));
        }

        if (holdNorm > peakNorm)
        {
            const auto holdY = usable.getBottom() - usable.getHeight() * holdNorm;
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.fillRect (juce::Rectangle<float> (usable.getX(), holdY - 0.5f, usable.getWidth(), 1.0f));
        }
    }

    // Clip-Feld über den Balken (Latch, Klick setzt zurück)
    g.setColour (lastClipped ? juce::Colour (0xffe14b3b)
                             : juce::Colour (0xff1b1e22));
    g.fillRoundedRectangle (clipZone(), 2.0f);
}

void GainFaderMeter::mouseDown (const juce::MouseEvent& event)
{
    if (! clipZone().contains (event.position))
        return;

    if (auto* module = dynamic_cast<ProcessorModule*> (graphManager.getModuleFor (nodeUuid)))
    {
        auto& meter = isInputMeter ? module->getInputMeter() : module->getOutputMeter();
        meter.resetClip (0);
        meter.resetClip (1);
        lastClipped = false;
        repaint();
    }
}

} // namespace conduit
