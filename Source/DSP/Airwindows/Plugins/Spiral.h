/* ========================================
 *  Spiral - Spiral.h
 *  Copyright (c) 2016 airwindows, Airwindows uses the MIT license
 *  ----------------------------------------
 *  Conduit-Port aus Third-Party/airwindows/plugins/LinuxVST/src/Spiral —
 *  DSP unverändert (Chris Johnson / Airwindows, MIT), nur Interface angepasst.
 * ======================================== */

#pragma once

#include "DSP/Airwindows/AirwindowsPlugin.h"

namespace conduit::airwindows
{

/** Spiral — Sinus-Saturation ohne Parameter (Clip bei 1.2533141373155 für
    maximalen Output). Zustandslos bis auf den fpd-Xorshift. */
class Spiral final : public AirwindowsPlugin
{
public:
    Spiral() noexcept;

protected:
    void processStereo (const float* in1, const float* in2,
                        float* out1, float* out2, int sampleFrames) noexcept override;
    void resetState() noexcept override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Spiral)
};

} // namespace conduit::airwindows
