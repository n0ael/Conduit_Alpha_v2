/* ========================================
 *  Slew - Slew.cpp
 *  Created 8/12/11 by SPIAdmin
 *  Copyright (c) 2011 __MyCompanyName__, Airwindows uses the MIT license
 *  ----------------------------------------
 *  Conduit-Port aus Third-Party/airwindows/plugins/LinuxVST/src/Slew —
 *  DSP unverändert (Chris Johnson / Airwindows, MIT), nur Interface angepasst.
 * ======================================== */

#include "DSP/Airwindows/Plugins/Slew.h"

#include <cmath>

namespace conduit::airwindows
{

namespace
{
    constexpr ParameterInfo kParameters[] = {
        { "clamping", "Clamping", 0.0f },
    };
}

Slew::Slew() noexcept
    : AirwindowsPlugin ("slew", "Slew", kParameters)
{
}

void Slew::resetState() noexcept
{
    lastSampleL = 0.0;
    lastSampleR = 0.0;
}

void Slew::processStereo (const float* in1, const float* in2,
                          float* out1, float* out2, int sampleFrames) noexcept
{
    const float gain = param (kSlewParam);

    double overallscale = 1.0;
    overallscale /= 44100.0;
    overallscale *= getSampleRate();

    double inputSampleL;
    double inputSampleR;
    double outputSampleL;
    double outputSampleR;

    double clamp;
    double threshold = std::pow ((1 - gain), 4) / overallscale;

    while (--sampleFrames >= 0)
    {
        inputSampleL = *in1;
        inputSampleR = *in2;
        if (std::fabs (inputSampleL) < 1.18e-23) inputSampleL = fpdL * 1.18e-17;
        if (std::fabs (inputSampleR) < 1.18e-23) inputSampleR = fpdR * 1.18e-17;

        clamp = inputSampleL - lastSampleL;
        outputSampleL = inputSampleL;
        if (clamp > threshold)
            outputSampleL = lastSampleL + threshold;
        if (-clamp > threshold)
            outputSampleL = lastSampleL - threshold;
        lastSampleL = outputSampleL;

        clamp = inputSampleR - lastSampleR;
        outputSampleR = inputSampleR;
        if (clamp > threshold)
            outputSampleR = lastSampleR + threshold;
        if (-clamp > threshold)
            outputSampleR = lastSampleR - threshold;
        lastSampleR = outputSampleR;

        *out1 = (float) outputSampleL;
        *out2 = (float) outputSampleR;

        ++in1;
        ++in2;
        ++out1;
        ++out2;
    }
}

} // namespace conduit::airwindows
