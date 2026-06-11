#include "ConduitModule.h"

namespace conduit
{

juce::String toString (ModuleType type)
{
    switch (type)
    {
        case ModuleType::generator: return "Generator";
        case ModuleType::processor: return "Processor";
        case ModuleType::io:        return "IO";
        case ModuleType::analysis:  return "Analysis";
        case ModuleType::utility:   return "Utility";
    }

    jassertfalse;
    return {};
}

juce::String toString (NodeState state)
{
    switch (state)
    {
        case NodeState::active:    return "Active";
        case NodeState::fadingOut: return "FadingOut";
        case NodeState::fadingIn:  return "FadingIn";
        case NodeState::deleting:  return "Deleting";
    }

    jassertfalse;
    return {};
}

//==============================================================================
ConduitModule::ConduitModule (const BusesProperties& buses)
    : juce::AudioProcessor (buses)
{
}

juce::ValueTree ConduitModule::createState()
{
    // Message Thread, VOR addNode() — nie im Audio-Callback (CLAUDE.md 4.4).
    juce::ValueTree nodeTree (id::node);

    nodeTree.setProperty (id::nodeId,       juce::Uuid().toString(),         nullptr);
    nodeTree.setProperty (id::type,         toString (getType()),            nullptr);
    nodeTree.setProperty (id::moduleId,     getModuleId(),                   nullptr);
    nodeTree.setProperty (id::stateVersion, getStateVersion(),               nullptr);
    nodeTree.setProperty (id::nodeState,    toString (NodeState::active),    nullptr);
    nodeTree.setProperty (id::nodeError,    juce::String(),                  nullptr);
    nodeTree.setProperty (id::positionX,    0.0,                             nullptr);
    nodeTree.setProperty (id::positionY,    0.0,                             nullptr);

    juce::ValueTree parameters (id::parameters);
    appendParametersTo (parameters);
    nodeTree.appendChild (parameters, nullptr);

    return nodeTree;
}

juce::Result ConduitModule::prepareForGraph (double sampleRate, int maximumBlockSize)
{
    setPlayConfigDetails (getTotalNumInputChannels(), getTotalNumOutputChannels(),
                          sampleRate, maximumBlockSize);
    prepareToPlay (sampleRate, maximumBlockSize);
    return juce::Result::ok();
}

void ConduitModule::appendParametersTo (juce::ValueTree&)
{
    // Default: Modul ohne Parameter.
}

juce::ValueTree ConduitModule::makeParameter (const juce::String& parameterId,
                                              double value,
                                              double minValue,
                                              double maxValue,
                                              double defaultValue)
{
    juce::ValueTree parameter (id::parameter);
    parameter.setProperty (id::paramId,      parameterId,  nullptr);
    parameter.setProperty (id::paramValue,   value,        nullptr);
    parameter.setProperty (id::paramMin,     minValue,     nullptr);
    parameter.setProperty (id::paramMax,     maxValue,     nullptr);
    parameter.setProperty (id::paramDefault, defaultValue, nullptr);
    return parameter;
}

//==============================================================================
const juce::String ConduitModule::getName() const            { return getModuleDisplayName(); }
bool ConduitModule::acceptsMidi() const                      { return false; }
bool ConduitModule::producesMidi() const                     { return false; }
double ConduitModule::getTailLengthSeconds() const           { return 0.0; }

int ConduitModule::getNumPrograms()                          { return 1; }
int ConduitModule::getCurrentProgram()                       { return 0; }
void ConduitModule::setCurrentProgram (int)                  {}
const juce::String ConduitModule::getProgramName (int)       { return {}; }
void ConduitModule::changeProgramName (int, const juce::String&) {}

bool ConduitModule::hasEditor() const                        { return false; }
juce::AudioProcessorEditor* ConduitModule::createEditor()    { return nullptr; }

void ConduitModule::getStateInformation (juce::MemoryBlock&) {}
void ConduitModule::setStateInformation (const void*, int)   {}

} // namespace conduit
