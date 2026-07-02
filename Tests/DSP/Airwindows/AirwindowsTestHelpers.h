/* ========================================
 *  AirwindowsTestHelpers.h — generische DoD-Tests für Airwindows-Ports:
 *  Null-Test, Blockgrößen-Invarianz (64 vs. 512), Parameter-Sweep
 * ======================================== */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "DSP/Airwindows/AirwindowsPlugin.h"

namespace airwindows_tests
{

// Deterministisches Rausch-Signal ±1 via LCG (CLAUDE.md 3.1-Konvention)
inline void fillNoise (std::vector<float>& buffer, std::uint32_t seed)
{
    std::uint32_t state = seed;
    for (auto& sample : buffer)
    {
        state = 1664525u * state + 1013904223u;
        sample = (float) ((double) state / 2147483648.0 - 1.0); // [-1, 1)
    }
}

inline void processInBlocks (conduit::airwindows::AirwindowsPlugin& plugin,
                             const std::vector<float>& inL, const std::vector<float>& inR,
                             std::vector<float>& outL, std::vector<float>& outR,
                             int blockSize)
{
    const int total = (int) inL.size();
    for (int pos = 0; pos < total; pos += blockSize)
    {
        const int n = std::min (blockSize, total - pos);
        plugin.process (inL.data() + pos, inR.data() + pos,
                        outL.data() + pos, outR.data() + pos, n);
    }
}

inline float maxAbs (const std::vector<float>& buffer)
{
    float m = 0.0f;
    for (float s : buffer)
        m = std::max (m, std::fabs (s));
    return m;
}

// true, wenn jedes Sample endlich und nicht subnormal ist
inline bool allFiniteAndNormal (const std::vector<float>& buffer)
{
    for (float s : buffer)
        if (! std::isfinite (s) || std::fpclassify (s) == FP_SUBNORMAL)
            return false;
    return true;
}

//==============================================================================
// DoD-Test 1: Stille rein (Dither off, Defaults) → Stille raus.
// Toleranz 1e-6 (−120 dBFS): der Denormal-Guard der Originale injiziert
// fpd * 1.18e-17 (max. ~5e-8) — bewusst kein exakter Null-Vergleich.
template <typename PluginType>
void runNullTest()
{
    PluginType plugin;
    plugin.prepare (48000.0);
    REQUIRE (! plugin.isDitherEnabled()); // Default OFF (CLAUDE.local.md)

    const std::vector<float> silence (4096, 0.0f);
    std::vector<float> outL (silence.size(), 1.0f), outR (silence.size(), 1.0f);

    processInBlocks (plugin, silence, silence, outL, outR, 64);

    REQUIRE (maxAbs (outL) < 1.0e-6f);
    REQUIRE (maxAbs (outR) < 1.0e-6f);
}

//==============================================================================
// DoD-Test 2: identisches Signal in 64er- vs. 512er-Blöcken → bitidentisch.
template <typename PluginType>
void runBlockSizeInvariance()
{
    auto renderWithBlockSize = [] (int blockSize, std::vector<float>& outL, std::vector<float>& outR)
    {
        PluginType plugin;
        plugin.prepare (48000.0);
        for (int i = 0; i < plugin.getNumParameters(); ++i)
            plugin.setParameter (i, 0.7f); // von den Defaults wegbewegen

        std::vector<float> inL (4096), inR (4096);
        fillNoise (inL, 0xC0FFEE01u);
        fillNoise (inR, 0xC0FFEE02u);

        outL.assign (inL.size(), 0.0f);
        outR.assign (inR.size(), 0.0f);
        processInBlocks (plugin, inL, inR, outL, outR, blockSize);
    };

    std::vector<float> smallL, smallR, largeL, largeR;
    renderWithBlockSize (64, smallL, smallR);
    renderWithBlockSize (512, largeL, largeR);

    REQUIRE (smallL == largeL);
    REQUIRE (smallR == largeR);
}

//==============================================================================
// DoD-Test 3: Vollkreuz-Sweep über alle Parameter (0/0.25/0.5/0.75/1),
// jeweils mit Dither off und on → kein NaN/Inf/Denormal im Output.
template <typename PluginType>
void runParameterSweep()
{
    constexpr float sweepValues[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    constexpr int numValues = 5;

    std::vector<float> inL (1024), inR (1024);
    fillNoise (inL, 0x5EED0001u);
    fillNoise (inR, 0x5EED0002u);
    std::vector<float> outL (inL.size()), outR (inR.size());

    const int numParams = PluginType().getNumParameters();
    int combos = 1;
    for (int i = 0; i < numParams; ++i)
        combos *= numValues;

    for (int ditherPass = 0; ditherPass < 2; ++ditherPass)
    {
        for (int combo = 0; combo < combos; ++combo)
        {
            PluginType plugin;
            plugin.prepare (48000.0);
            plugin.setDitherEnabled (ditherPass == 1);

            int digits = combo;
            for (int i = 0; i < numParams; ++i)
            {
                plugin.setParameter (i, sweepValues[digits % numValues]);
                digits /= numValues;
            }

            processInBlocks (plugin, inL, inR, outL, outR, 512);

            CAPTURE (combo, ditherPass);
            REQUIRE (allFiniteAndNormal (outL));
            REQUIRE (allFiniteAndNormal (outR));
        }
    }
}

} // namespace airwindows_tests
