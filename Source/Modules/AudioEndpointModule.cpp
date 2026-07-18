#include "Modules/AudioEndpointModule.h"

namespace conduit
{

AudioEndpointModule::AudioEndpointModule (Direction directionToUse)
    : ConduitModule (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      direction (directionToUse)
{
}

//==============================================================================
juce::String AudioEndpointModule::getModuleId() const
{
    return direction == Direction::input ? audioInputModuleId : audioOutputModuleId;
}

juce::String AudioEndpointModule::getModuleDisplayName() const
{
    return direction == Direction::input
        ? juce::String::fromUTF8 ("Audio-Eingang")
        : juce::String::fromUTF8 ("Audio-Ausgang");
}

ModuleType AudioEndpointModule::getType() const  { return ModuleType::io; }
int AudioEndpointModule::getStateVersion() const { return 1; }

juce::ValueTree AudioEndpointModule::createState()
{
    auto nodeTree = ConduitModule::createState();

    // Port-Sicht des Schemas 6.2: die Hardware LIEFERT Kanäle → der
    // audio_input-Node zeigt nur Ausgangs-Ports; audio_output umgekehrt.
    // (Die Graph-Busse bleiben N/N — Pass-Through.)
    nodeTree.setProperty (id::numInputChannels,
                          direction == Direction::input ? 0 : channels, nullptr);
    nodeTree.setProperty (id::numOutputChannels,
                          direction == Direction::input ? channels : 0, nullptr);
    return nodeTree;
}

//==============================================================================
bool AudioEndpointModule::isInputEndpoint() const noexcept
{
    return direction == Direction::input;
}

void AudioEndpointModule::setEndpointChannels (int numChannels)
{
    channels = juce::jmax (1, numChannels);
}

int AudioEndpointModule::getEndpointChannels() const noexcept
{
    return channels;
}

juce::Result AudioEndpointModule::prepareForGraph (double sampleRate, int maximumBlockSize)
{
    // Pass-Through mit Hardware-Kanalzahl (setEndpointChannels lief davor)
    setPlayConfigDetails (channels, channels, sampleRate, maximumBlockSize);
    prepareToPlay (sampleRate, maximumBlockSize);
    return juce::Result::ok();
}

//==============================================================================
void AudioEndpointModule::prepareToPlay (double, int) {}
void AudioEndpointModule::releaseResources() {}

void AudioEndpointModule::processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&)
{
    // Pass-Through: der Graph liefert den Input im Buffer, der Output IST
    // der Buffer — nichts zu tun (lock-free, allocation-free, CLAUDE.md 3.1)
}

} // namespace conduit
