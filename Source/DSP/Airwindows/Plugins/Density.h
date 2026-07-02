/* ========================================
 *  Density - Density.h
 *  Copyright (c) 2016 airwindows, Airwindows uses the MIT license
 *  ----------------------------------------
 *  Conduit-Port aus Third-Party/airwindows/plugins/LinuxVST/src/Density —
 *  DSP unverändert (Chris Johnson / Airwindows, MIT), nur Interface angepasst.
 * ======================================== */

#pragma once

#include "DSP/Airwindows/AirwindowsPlugin.h"

namespace conduit::airwindows
{

/** Density — Sinus-Saturation mit Density-Regler (Default 0.2 entspricht
    Density 0.0 auf der internen Skala), Highpass, Out Level und Dry/Wet. */
class Density final : public AirwindowsPlugin
{
public:
    enum Params
    {
        kParamA = 0, // Density
        kParamB = 1, // Highpass
        kParamC = 2, // Out Level
        kParamD = 3, // Dry/Wet
        kNumParameters = 4
    };

    Density() noexcept;

protected:
    void processStereo (const float* in1, const float* in2,
                        float* out1, float* out2, int sampleFrames) noexcept override;
    void resetState() noexcept override;

private:
    double iirSampleAL = 0.0;
    double iirSampleBL = 0.0;
    double iirSampleAR = 0.0;
    double iirSampleBR = 0.0;
    bool fpFlip = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Density)
};

} // namespace conduit::airwindows
