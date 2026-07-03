/* ========================================
 *  OneCornerClipTests.cpp — DoD-Tests für den OneCornerClip-Port (CLAUDE.local.md)
 *
 *  Hinweis Blockgroessen-Invarianz: dieses Original interpoliert Parameter
 *  block-intern (inFramesToProcess) — der Einschwingverlauf des ersten
 *  Blocks haengt von der Blockgroesse ab und die DSP-Zustaende divergieren
 *  danach dauerhaft minimal (Original-VST-Verhalten, kein Portierungs-
 *  fehler). Der Invarianz-Test entfaellt daher (Muster ConsoleLABuss/
 *  ConsoleMCBuss, siehe PORTING_NOTES.md).
 * ======================================== */

#include "AirwindowsTestHelpers.h"
#include "DSP/Airwindows/Plugins/OneCornerClip.h"

using conduit::airwindows::OneCornerClip;

TEST_CASE ("Airwindows OneCornerClip: Null-Test (Silence -> Silence, Dither off)", "[airwindows][dsp]")
{
    airwindows_tests::runNullTest<OneCornerClip>();
}

TEST_CASE ("Airwindows OneCornerClip: Parameter-Sweep ohne NaN/Inf/Denormals", "[airwindows][dsp]")
{
    airwindows_tests::runParameterSweep<OneCornerClip>();
}
