#pragma once

#include "ConduitModule.h"

namespace conduit
{

//==============================================================================
/** Basisklasse aller Utility-Module (CLAUDE.md 4.1):
    Mixer, Attenuator, DC Block, Math, Offset. */
class UtilityModule : public ConduitModule
{
public:
    using ConduitModule::ConduitModule;

    [[nodiscard]] ModuleType getType() const override { return ModuleType::utility; }
};

} // namespace conduit
