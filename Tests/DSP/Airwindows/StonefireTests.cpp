/* ========================================
 *  StonefireTests.cpp — DoD-Tests für den Stonefire-Port (CLAUDE.local.md)
 *
 *  Hinweis Blockgroessen-Invarianz: dieses Original interpoliert Parameter
 *  block-intern (inFramesToProcess) — der Einschwingverlauf des ersten
 *  Blocks haengt von der Blockgroesse ab und die DSP-Zustaende divergieren
 *  danach dauerhaft minimal (Original-VST-Verhalten, kein Portierungs-
 *  fehler). Der Invarianz-Test entfaellt daher (Muster ConsoleLABuss/
 *  ConsoleMCBuss, siehe PORTING_NOTES.md).
 * ======================================== */

#include "AirwindowsTestHelpers.h"
#include "DSP/Airwindows/Plugins/Stonefire.h"

using conduit::airwindows::Stonefire;

TEST_CASE ("Airwindows Stonefire: Null-Test (Silence -> Silence, Dither off)", "[airwindows][dsp]")
{
    airwindows_tests::runNullTest<Stonefire>();
}

TEST_CASE ("Airwindows Stonefire: Parameter-Sweep ohne NaN/Inf/Denormals", "[airwindows][dsp]")
{
    airwindows_tests::runParameterSweep<Stonefire>();
}
