#include "ModuleFactory.h"

#include "AttenuatorModule.h"
#include "LfoModule.h"
#include "ScopeModule.h"
#include "StepSequencerModule.h"

namespace conduit
{

void ModuleFactory::registerModule (const juce::String& moduleId, Creator creator)
{
    jassert (creator != nullptr);
    jassert (moduleId.isNotEmpty());

    creators[moduleId] = std::move (creator);
}

bool ModuleFactory::isRegistered (const juce::String& moduleId) const
{
    return creators.contains (moduleId);
}

std::unique_ptr<ConduitModule> ModuleFactory::create (const juce::String& moduleId) const
{
    if (const auto it = creators.find (moduleId); it != creators.end())
        return (it->second)();

    return nullptr;
}

//==============================================================================
void registerDefaultModules (ModuleFactory& factory)
{
    factory.registerModule (AttenuatorModule::staticModuleId,
                            [] { return std::make_unique<AttenuatorModule>(); });
    factory.registerModule (LfoModule::staticModuleId,
                            [] { return std::make_unique<LfoModule>(); });
    factory.registerModule (ScopeModule::staticModuleId,
                            [] { return std::make_unique<ScopeModule>(); });
    factory.registerModule (StepSequencerModule::staticModuleId,
                            [] { return std::make_unique<StepSequencerModule>(); });
}

} // namespace conduit
