#include "ScopeModule.h"

#include <algorithm>

namespace conduit
{

ScopeModule::ScopeModule()
    : AnalysisModule (BusesProperties()
          .withInput  ("Input", juce::AudioChannelSet::mono(), true)
          .withOutput ("Thru",  juce::AudioChannelSet::mono(), true))
{
}

//==============================================================================
juce::String ScopeModule::getModuleId() const          { return staticModuleId; }
juce::String ScopeModule::getModuleDisplayName() const { return "Scope"; }
int ScopeModule::getStateVersion() const               { return 1; }

//==============================================================================
void ScopeModule::prepareToPlay (double, int)
{
    binCount = 0;
}

void ScopeModule::releaseResources()
{
}

void ScopeModule::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Pass-Through: der Graph liefert den Input in Kanal 0, der Thru-Bus
    // erwartet ihn dort — der Puffer bleibt unangetastet.
    const auto* samples = buffer.getReadPointer (0);
    const auto numSamples = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        if (binCount == 0)
        {
            binMin = samples[i];
            binMax = samples[i];
        }
        else
        {
            binMin = std::min (binMin, samples[i]);
            binMax = std::max (binMax, samples[i]);
        }

        if (++binCount >= binSize)
        {
            // Volle Queue (keine UI zieht): Bin verwerfen — kein Block, kein Crash
            scopeQueue.push ({ binMin, binMax });
            binCount = 0;
        }
    }
}

} // namespace conduit
