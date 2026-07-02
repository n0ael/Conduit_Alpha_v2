/* ========================================
 *  AirwindowsPlugin.cpp — Conduit-Basis für Airwindows-Portierungen
 *  DSP-Originale: Chris Johnson / Airwindows (MIT-Lizenz)
 * ======================================== */

#include "DSP/Airwindows/AirwindowsPlugin.h"

namespace conduit::airwindows
{

AirwindowsPlugin::AirwindowsPlugin (const char* effectIdIn, const char* effectNameIn,
                                    std::span<const ParameterInfo> parameters) noexcept
    : effectId (effectIdIn),
      effectName (effectNameIn),
      parameterInfos (parameters)
{
    jassert (parameters.size() <= (size_t) maxParameters);

    for (size_t i = 0; i < parameterInfos.size(); ++i)
    {
        parameterValues[i].store (parameterInfos[i].defaultValue, std::memory_order_relaxed);
        paramSnapshot[i] = parameterInfos[i].defaultValue;
    }
}

void AirwindowsPlugin::setParameter (int index, float value01) noexcept
{
    if (index < 0 || index >= getNumParameters())
    {
        jassertfalse;
        return;
    }

    const float clamped = value01 < 0.0f ? 0.0f : (value01 > 1.0f ? 1.0f : value01);
    parameterValues[(size_t) index].store (clamped, std::memory_order_relaxed);
}

float AirwindowsPlugin::getParameter (int index) const noexcept
{
    if (index < 0 || index >= getNumParameters())
    {
        jassertfalse;
        return 0.0f;
    }

    return parameterValues[(size_t) index].load (std::memory_order_relaxed);
}

void AirwindowsPlugin::prepare (double newSampleRate,
                                std::uint32_t seedL, std::uint32_t seedR) noexcept
{
    sampleRate = newSampleRate;

    // Original: fpd = 1.0; while (fpd < 16386) fpd = rand()*UINT32_MAX;
    // Deterministisch statt rand() (CLAUDE.md 3.1) — die 16386-Untergrenze bleibt.
    fpdL = seedL < 16386u ? seedL + 16386u : seedL;
    fpdR = seedR < 16386u ? seedR + 16386u : seedR;

    resetState();
}

void AirwindowsPlugin::process (const float* inL, const float* inR,
                                float* outL, float* outR, int numSamples) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    for (size_t i = 0; i < parameterInfos.size(); ++i)
        paramSnapshot[i] = parameterValues[i].load (std::memory_order_relaxed);

    ditherSnapshot = ditherEnabled.load (std::memory_order_relaxed);

    processStereo (inL, inR, outL, outR, numSamples);
}

} // namespace conduit::airwindows
