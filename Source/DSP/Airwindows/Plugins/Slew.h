/* ========================================
 *  Slew - Slew.h
 *  Created 8/12/11 by SPIAdmin
 *  Copyright (c) 2011 __MyCompanyName__, Airwindows uses the MIT license
 *  ----------------------------------------
 *  Conduit-Port aus Third-Party/airwindows/plugins/LinuxVST/src/Slew —
 *  DSP unverändert (Chris Johnson / Airwindows, MIT), nur Interface angepasst.
 * ======================================== */

#pragma once

#include "DSP/Airwindows/AirwindowsPlugin.h"

namespace conduit::airwindows
{

/** Slew — Slew-Rate-Limiter / "Clamping".

    Besonderheiten des Originals (beibehalten, siehe PORTING_NOTES.md):
    kein fpd-Dither am Ausgang, fpd wird nie advanced (dient nur als
    konstanter Denormal-Guard) — setDitherEnabled ist hier wirkungslos.
*/
class Slew final : public AirwindowsPlugin
{
public:
    enum Params
    {
        kSlewParam = 0,
        kNumParameters = 1
    };

    Slew() noexcept;

protected:
    void processStereo (const float* in1, const float* in2,
                        float* out1, float* out2, int sampleFrames) noexcept override;
    void resetState() noexcept override;

private:
    double lastSampleL = 0.0;
    double lastSampleR = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Slew)
};

} // namespace conduit::airwindows
