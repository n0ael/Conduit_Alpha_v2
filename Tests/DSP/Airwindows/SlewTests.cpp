/* ========================================
 *  SlewTests.cpp — DoD-Tests für den Slew-Port (CLAUDE.local.md)
 * ======================================== */

#include "AirwindowsTestHelpers.h"
#include "DSP/Airwindows/Plugins/Slew.h"

using conduit::airwindows::Slew;

TEST_CASE ("Airwindows Slew: Null-Test (Silence -> Silence, Dither off)", "[airwindows][dsp]")
{
    airwindows_tests::runNullTest<Slew>();
}

TEST_CASE ("Airwindows Slew: Blockgroessen-Invarianz 64 vs. 512", "[airwindows][dsp]")
{
    airwindows_tests::runBlockSizeInvariance<Slew>();
}

TEST_CASE ("Airwindows Slew: Parameter-Sweep ohne NaN/Inf/Denormals", "[airwindows][dsp]")
{
    airwindows_tests::runParameterSweep<Slew>();
}
