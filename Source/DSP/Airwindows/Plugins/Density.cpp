/* ========================================
 *  Density - Density.cpp
 *  Copyright (c) 2016 airwindows, Airwindows uses the MIT license
 *  ----------------------------------------
 *  Conduit-Port aus Third-Party/airwindows/plugins/LinuxVST/src/Density —
 *  DSP unverändert (Chris Johnson / Airwindows, MIT), nur Interface angepasst.
 *  Der fpd-Dither-Add hängt an ditherOn(); der Xorshift läuft immer weiter
 *  (entspricht dem processDoubleReplacing-Pfad des Originals).
 * ======================================== */

#include "DSP/Airwindows/Plugins/Density.h"

#include <cmath>
#include <cstdint>

namespace conduit::airwindows
{

namespace
{
    constexpr ParameterInfo kParameters[] = {
        { "density",   "Density",   0.2f }, // 0.2 == Density 0.0 auf der internen Skala
        { "highpass",  "Highpass",  0.0f },
        { "out_level", "Out Level", 1.0f },
        { "dry_wet",   "Dry/Wet",   1.0f },
    };
}

Density::Density() noexcept
    : AirwindowsPlugin ("density", "Density", kParameters)
{
}

void Density::resetState() noexcept
{
    iirSampleAL = 0.0;
    iirSampleBL = 0.0;
    iirSampleAR = 0.0;
    iirSampleBR = 0.0;
    fpFlip = true;
}

void Density::processStereo (const float* in1, const float* in2,
                             float* out1, float* out2, int sampleFrames) noexcept
{
    const float A = param (kParamA);
    const float B = param (kParamB);
    const float C = param (kParamC);
    const float D = param (kParamD);

    double overallscale = 1.0;
    overallscale /= 44100.0;
    overallscale *= getSampleRate();
    double density = (A * 5.0) - 1.0;
    double iirAmount = std::pow (B, 3) / overallscale;
    double output = C;
    double wet = D;
    double dry = 1.0 - wet;
    double bridgerectifier;
    double out = std::fabs (density);
    density = density * std::fabs (density);
    double count;

    double inputSampleL;
    double inputSampleR;
    double drySampleL;
    double drySampleR;

    while (--sampleFrames >= 0)
    {
        inputSampleL = *in1;
        inputSampleR = *in2;
        if (std::fabs (inputSampleL) < 1.18e-23) inputSampleL = fpdL * 1.18e-17;
        if (std::fabs (inputSampleR) < 1.18e-23) inputSampleR = fpdR * 1.18e-17;
        drySampleL = inputSampleL;
        drySampleR = inputSampleR;

        if (fpFlip)
        {
            iirSampleAL = (iirSampleAL * (1.0 - iirAmount)) + (inputSampleL * iirAmount);
            inputSampleL -= iirSampleAL;
            iirSampleAR = (iirSampleAR * (1.0 - iirAmount)) + (inputSampleR * iirAmount);
            inputSampleR -= iirSampleAR;
        }
        else
        {
            iirSampleBL = (iirSampleBL * (1.0 - iirAmount)) + (inputSampleL * iirAmount);
            inputSampleL -= iirSampleBL;
            iirSampleBR = (iirSampleBR * (1.0 - iirAmount)) + (inputSampleR * iirAmount);
            inputSampleR -= iirSampleBR;
        }
        //highpass section
        fpFlip = !fpFlip;

        count = density;
        while (count > 1.0)
        {
            bridgerectifier = std::fabs (inputSampleL) * 1.57079633;
            if (bridgerectifier > 1.57079633) bridgerectifier = 1.57079633;
            //max value for sine function
            bridgerectifier = std::sin (bridgerectifier);
            if (inputSampleL > 0.0) inputSampleL = bridgerectifier;
            else inputSampleL = -bridgerectifier;

            bridgerectifier = std::fabs (inputSampleR) * 1.57079633;
            if (bridgerectifier > 1.57079633) bridgerectifier = 1.57079633;
            //max value for sine function
            bridgerectifier = std::sin (bridgerectifier);
            if (inputSampleR > 0.0) inputSampleR = bridgerectifier;
            else inputSampleR = -bridgerectifier;

            count = count - 1.0;
        }
        //we have now accounted for any really high density settings.

        while (out > 1.0) out = out - 1.0;

        bridgerectifier = std::fabs (inputSampleL) * 1.57079633;
        if (bridgerectifier > 1.57079633) bridgerectifier = 1.57079633;
        //max value for sine function
        if (density > 0) bridgerectifier = std::sin (bridgerectifier);
        else bridgerectifier = 1 - std::cos (bridgerectifier);
        //produce either boosted or starved version
        if (inputSampleL > 0) inputSampleL = (inputSampleL * (1 - out)) + (bridgerectifier * out);
        else inputSampleL = (inputSampleL * (1 - out)) - (bridgerectifier * out);
        //blend according to density control

        bridgerectifier = std::fabs (inputSampleR) * 1.57079633;
        if (bridgerectifier > 1.57079633) bridgerectifier = 1.57079633;
        //max value for sine function
        if (density > 0) bridgerectifier = std::sin (bridgerectifier);
        else bridgerectifier = 1 - std::cos (bridgerectifier);
        //produce either boosted or starved version
        if (inputSampleR > 0) inputSampleR = (inputSampleR * (1.0 - out)) + (bridgerectifier * out);
        else inputSampleR = (inputSampleR * (1.0 - out)) - (bridgerectifier * out);
        //blend according to density control

        if (output < 1.0)
        {
            inputSampleL *= output;
            inputSampleR *= output;
        }
        if (wet < 1.0)
        {
            inputSampleL = (drySampleL * dry) + (inputSampleL * wet);
            inputSampleR = (drySampleR * dry) + (inputSampleR * wet);
        }
        //nice little output stage template: if we have another scale of floating point
        //number, we really don't want to meaninglessly multiply that by 1.0.

        //begin 32 bit stereo floating point dither
        if (ditherOn())
        {
            int expon;
            std::frexp ((float) inputSampleL, &expon);
            fpdL ^= fpdL << 13; fpdL ^= fpdL >> 17; fpdL ^= fpdL << 5;
            inputSampleL += (double) ((double (fpdL) - std::uint32_t (0x7fffffff)) * 5.5e-36l * std::pow (2, expon + 62));
            std::frexp ((float) inputSampleR, &expon);
            fpdR ^= fpdR << 13; fpdR ^= fpdR >> 17; fpdR ^= fpdR << 5;
            inputSampleR += (double) ((double (fpdR) - std::uint32_t (0x7fffffff)) * 5.5e-36l * std::pow (2, expon + 62));
        }
        else
        {
            fpdL ^= fpdL << 13; fpdL ^= fpdL >> 17; fpdL ^= fpdL << 5;
            fpdR ^= fpdR << 13; fpdR ^= fpdR >> 17; fpdR ^= fpdR << 5;
        }
        //end 32 bit stereo floating point dither

        *out1 = (float) inputSampleL;
        *out2 = (float) inputSampleR;

        ++in1;
        ++in2;
        ++out1;
        ++out2;
    }
}

} // namespace conduit::airwindows
