/* ========================================
 *  DeBezTests.cpp — DoD-Tests für den DeBez-Port (CLAUDE.local.md)
 *
 *  Hinweis Blockgroessen-Invarianz: dieses Original interpoliert Parameter
 *  block-intern (inFramesToProcess) — der Einschwingverlauf des ersten
 *  Blocks haengt von der Blockgroesse ab und die DSP-Zustaende divergieren
 *  danach dauerhaft minimal (Original-VST-Verhalten, kein Portierungs-
 *  fehler). Der Invarianz-Test entfaellt daher (Muster ConsoleLABuss/
 *  ConsoleMCBuss, siehe PORTING_NOTES.md).
 * ======================================== */

#include "AirwindowsTestHelpers.h"
#include "DSP/Airwindows/Plugins/DeBez.h"

using conduit::airwindows::DeBez;

TEST_CASE ("Airwindows DeBez: Null-Test (Silence -> Silence, Dither off)", "[airwindows][dsp]")
{
    airwindows_tests::runNullTest<DeBez>();
}

TEST_CASE ("Airwindows DeBez: Parameter-Sweep ohne NaN/Inf/Denormals", "[airwindows][dsp]")
{
    airwindows_tests::runParameterSweep<DeBez>();
}
