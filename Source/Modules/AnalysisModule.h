#pragma once

#include "ConduitModule.h"

namespace conduit
{

//==============================================================================
/** Basisklasse für Mess-/Anzeige-Module: Scope, Tuner, FFT, CVTunerModule
    (CLAUDE.md 4.1). */
class AnalysisModule : public ConduitModule
{
public:
    using ConduitModule::ConduitModule;

    [[nodiscard]] ModuleType getType() const override { return ModuleType::analysis; }
};

} // namespace conduit
