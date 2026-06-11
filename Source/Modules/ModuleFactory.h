#pragma once

#include <functional>
#include <map>
#include <memory>

#include "ConduitModule.h"

namespace conduit
{

//==============================================================================
/**
    Erzeugt Modul-Instanzen aus persistierten moduleIds (Schema 6.2).

    Der GraphManager materialisiert darüber Module beim Graph-Swap —
    derselbe Pfad gilt für User-Add, Undo/Redo und Preset-Load.

    Nur Message Thread.
*/
class ModuleFactory final
{
public:
    using Creator = std::function<std::unique_ptr<ConduitModule>()>;

    ModuleFactory() = default;

    void registerModule (const juce::String& moduleId, Creator creator);

    [[nodiscard]] bool isRegistered (const juce::String& moduleId) const;

    /** nullptr, wenn die moduleId unbekannt ist — der GraphManager setzt
        dann nodeError im ValueTree. */
    [[nodiscard]] std::unique_ptr<ConduitModule> create (const juce::String& moduleId) const;

private:
    std::map<juce::String, Creator> creators;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleFactory)
};

/** Registriert alle eingebauten Module. */
void registerDefaultModules (ModuleFactory& factory);

} // namespace conduit
