/* ========================================
 *  DeRez3Tests.cpp — DoD-Tests für den DeRez3-Port (CLAUDE.local.md)
 *
 *  Hinweis Blockgroessen-Invarianz: dieses Original interpoliert Parameter
 *  block-intern (inFramesToProcess) — der Einschwingverlauf des ersten
 *  Blocks haengt von der Blockgroesse ab und die DSP-Zustaende divergieren
 *  danach dauerhaft minimal (Original-VST-Verhalten, kein Portierungs-
 *  fehler). Der Invarianz-Test entfaellt daher (Muster ConsoleLABuss/
 *  ConsoleMCBuss, siehe PORTING_NOTES.md).
 * ======================================== */

#include "AirwindowsTestHelpers.h"
#include "DSP/Airwindows/Plugins/DeRez3.h"

using conduit::airwindows::DeRez3;

TEST_CASE ("Airwindows DeRez3: Null-Test (Silence -> Silence, Dither off)", "[airwindows][dsp]")
{
    airwindows_tests::runNullTest<DeRez3>();
}

TEST_CASE ("Airwindows DeRez3: Parameter-Sweep ohne NaN/Inf/Denormals", "[airwindows][dsp]")
{
    airwindows_tests::runParameterSweep<DeRez3>();
}
