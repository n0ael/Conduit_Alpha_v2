#include "ModuleFactory.h"

#include "AttenuatorModule.h"

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
}

} // namespace conduit
