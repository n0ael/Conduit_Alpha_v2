/* ========================================
 *  SpiralTests.cpp — DoD-Tests für den Spiral-Port (CLAUDE.local.md)
 * ======================================== */

#include "AirwindowsTestHelpers.h"
#include "DSP/Airwindows/Plugins/Spiral.h"

using conduit::airwindows::Spiral;

TEST_CASE ("Airwindows Spiral: Null-Test (Silence -> Silence, Dither off)", "[airwindows][dsp]")
{
    airwindows_tests::runNullTest<Spiral>();
}

TEST_CASE ("Airwindows Spiral: Blockgroessen-Invarianz 64 vs. 512", "[airwindows][dsp]")
{
    airwindows_tests::runBlockSizeInvariance<Spiral>();
}

TEST_CASE ("Airwindows Spiral: Parameter-Sweep ohne NaN/Inf/Denormals", "[airwindows][dsp]")
{
    airwindows_tests::runParameterSweep<Spiral>();
}
