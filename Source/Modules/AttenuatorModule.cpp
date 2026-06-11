#include "AttenuatorModule.h"

namespace conduit
{

AttenuatorModule::AttenuatorModule()
    : UtilityModule (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

//==============================================================================
juce::String AttenuatorModule::getModuleId() const          { return staticModuleId; }
juce::String AttenuatorModule::getModuleDisplayName() const { return "Attenuator"; }
int AttenuatorModule::getStateVersion() const               { return 1; }

void AttenuatorModule::appendParametersTo (juce::ValueTree& parameters)
{
    parameters.appendChild (makeParameter ("gain", 1.0, 0.0, 1.0, 1.0), nullptr);
}

//==============================================================================
void AttenuatorModule::setGain (float newGain) noexcept
{
    gainTarget.store (juce::jlimit (0.0f, 1.0f, newGain), std::memory_order_relaxed);
}

//==============================================================================
void AttenuatorModule::prepareToPlay (double sampleRate, int)
{
    smoothedGain.reset (sampleRate, 0.005);
    smoothedGain.setCurrentAndTargetValue (gainTarget.load (std::memory_order_relaxed));
}

void AttenuatorModule::releaseResources()
{
}

void AttenuatorModule::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    smoothedGain.setTargetValue (gainTarget.load (std::memory_order_relaxed));
    smoothedGain.applyGain (buffer, buffer.getNumSamples());
}

} // namespace conduit
