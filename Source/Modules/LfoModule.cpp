#include "LfoModule.h"

#include <cmath>

namespace conduit
{

LfoModule::LfoModule()
    : GeneratorModule (BusesProperties()
          .withOutput ("CV", juce::AudioChannelSet::mono(), true))
{
}

//==============================================================================
juce::String LfoModule::getModuleId() const          { return staticModuleId; }
juce::String LfoModule::getModuleDisplayName() const { return "LFO"; }
int LfoModule::getStateVersion() const               { return 1; }

void LfoModule::appendParametersTo (juce::ValueTree& parameters)
{
    parameters.appendChild (makeParameter ("rate",  0.25, 0.0625, 4.0, 0.25), nullptr);
    parameters.appendChild (makeParameter ("depth", 1.0,  0.0,    1.0, 1.0),  nullptr);
}

std::atomic<float>* LfoModule::getParameterTarget (const juce::String& parameterId) noexcept
{
    if (parameterId == "rate")  return &rateTarget;
    if (parameterId == "depth") return &depthTarget;
    return nullptr;
}

//==============================================================================
void LfoModule::setClockBus (const ClockBus* bus) noexcept
{
    clockBus = bus;
}

//==============================================================================
void LfoModule::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    smoothedDepth.reset (sampleRate, 0.005);
    smoothedDepth.setCurrentAndTargetValue (depthTarget.load (std::memory_order_relaxed));
}

void LfoModule::releaseResources()
{
}

void LfoModule::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto rate = static_cast<double> (rateTarget.load (std::memory_order_relaxed));
    smoothedDepth.setTargetValue (depthTarget.load (std::memory_order_relaxed));

    auto* out = buffer.getWritePointer (0);
    constexpr auto twoPi = juce::MathConstants<double>::twoPi;

    if (clockBus != nullptr)
    {
        // Beat-gelockt: Phase = Session-Beat × rate — deterministisch und
        // phasenstarr über alle Slaves/Peers (Link-Session-Zeitachse)
        const auto& clock = clockBus->current;
        const auto beatsPerSample = clock.beatsPerSample();

        for (int i = 0; i < numSamples; ++i)
        {
            const auto phase = (clock.beatAtBlockStart + i * beatsPerSample) * rate;
            out[i] = static_cast<float> (std::sin (phase * twoPi)) * smoothedDepth.getNextValue();
        }

        // Übergabepunkt, falls der Bus wegfällt (Tests/Re-Patch)
        freeRunPhase = (clock.beatAtBlockStart + numSamples * beatsPerSample) * rate;
    }
    else
    {
        // Freilauf: rate als Hz, Phase klick-frei akkumuliert
        const auto phasePerSample = currentSampleRate > 0.0 ? rate / currentSampleRate : 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            out[i] = static_cast<float> (std::sin (freeRunPhase * twoPi)) * smoothedDepth.getNextValue();
            freeRunPhase += phasePerSample;
        }

        freeRunPhase -= std::floor (freeRunPhase);  // begrenzen, ohne Phasensprung
    }

    // Weitere Output-Kanäle gibt es nicht (mono CV-Bus)
}

} // namespace conduit
