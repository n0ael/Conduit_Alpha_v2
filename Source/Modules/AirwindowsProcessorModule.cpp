#include "AirwindowsProcessorModule.h"

namespace conduit
{

std::vector<ChassisParamDesc> AirwindowsProcessorModule::makeDescs (const airwindows::AirwindowsPlugin& plugin)
{
    std::vector<ChassisParamDesc> descs;
    descs.reserve (static_cast<size_t> (plugin.getNumParameters()));

    for (int i = 0; i < plugin.getNumParameters(); ++i)
    {
        const auto& info = plugin.getParameterInfo (i);
        descs.push_back ({ info.id, info.defaultValue, 0.0f, 1.0f });  // Airwindows-Konvention 0..1
    }

    return descs;
}

AirwindowsProcessorModule::AirwindowsProcessorModule (std::unique_ptr<airwindows::AirwindowsPlugin> pluginToUse,
                                                      juce::String moduleIdToUse,
                                                      juce::String displayNameToUse)
    : ProcessorModule (makeDescs (*pluginToUse)),
      plugin (std::move (pluginToUse)),
      moduleIdString (std::move (moduleIdToUse)),
      displayNameString (std::move (displayNameToUse))
{
}

//==============================================================================
juce::String AirwindowsProcessorModule::getModuleId() const          { return moduleIdString; }
juce::String AirwindowsProcessorModule::getModuleDisplayName() const { return displayNameString; }
int AirwindowsProcessorModule::getStateVersion() const                { return chassisStateVersion; }

//==============================================================================
void AirwindowsProcessorModule::prepareCore (double sampleRate, int)
{
    plugin->prepare (sampleRate);
}

void AirwindowsProcessorModule::processCore (juce::AudioBuffer<float>& audio, juce::MidiBuffer&)
{
    for (int i = 0; i < plugin->getNumParameters(); ++i)
        plugin->setParameter (i, effectiveParam (i));

    auto* left  = audio.getWritePointer (0);
    auto* right = audio.getWritePointer (1);

    plugin->process (left, right, left, right, audio.getNumSamples());
}

} // namespace conduit
