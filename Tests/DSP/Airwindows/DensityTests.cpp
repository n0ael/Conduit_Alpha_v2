/* ========================================
 *  DensityTests.cpp — DoD-Tests für den Density-Port (CLAUDE.local.md)
 * ======================================== */

#include "AirwindowsTestHelpers.h"
#include "DSP/Airwindows/Plugins/Density.h"

using conduit::airwindows::Density;

TEST_CASE ("Airwindows Density: Null-Test (Silence -> Silence, Dither off)", "[airwindows][dsp]")
{
    airwindows_tests::runNullTest<Density>();
}

TEST_CASE ("Airwindows Density: Blockgroessen-Invarianz 64 vs. 512", "[airwindows][dsp]")
{
    airwindows_tests::runBlockSizeInvariance<Density>();
}

TEST_CASE ("Airwindows Density: Parameter-Sweep ohne NaN/Inf/Denormals", "[airwindows][dsp]")
{
    airwindows_tests::runParameterSweep<Density>();
}
