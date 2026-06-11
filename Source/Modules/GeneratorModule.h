#pragma once

#include "ConduitModule.h"

namespace conduit
{

//==============================================================================
/** Basisklasse für CV-/Trigger-Quellen: LFO, Envelope, Sequencer, MIDI→CV
    (CLAUDE.md 4.1). */
class GeneratorModule : public ConduitModule
{
public:
    using ConduitModule::ConduitModule;

    [[nodiscard]] ModuleType getType() const override { return ModuleType::generator; }
};

} // namespace conduit
