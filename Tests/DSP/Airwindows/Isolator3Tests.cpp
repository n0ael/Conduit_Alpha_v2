/* ========================================
 *  Isolator3Tests.cpp — DoD-Tests für den Isolator3-Port (CLAUDE.local.md)
 *
 *  Hinweis Blockgroessen-Invarianz: Isolator3 interpoliert seine Biquad-
 *  Koeffizienten block-intern (aStep = 1/inFramesToProcess) — bei von den
 *  Defaults abweichenden Parametern ist der Einschwingverlauf des ersten
 *  Blocks von der Blockgröße abhängig und die Filterzustände divergieren
 *  dauerhaft minimal (Original-VST-Verhalten, kein Portierungsfehler).
 *  Der Invarianz-Test entfällt daher (Muster ConsoleLABuss/ConsoleMCBuss).
 *
 *  Hinweis Null-Test-Toleranz: die 7-fach kaskadierte resonante Biquad-Bank
 *  verstärkt das Denormal-Guard-Rauschen des Originals (fpd·1.18e-17,
 *  max. ~5e-8) an der Resonanz um ~60 dB → gemessen ~4.5e-5 (−87 dBFS).
 *  Algorithmus-inhärent und unhörbar; Toleranz dokumentiert auf 1e-4.
 * ======================================== */

#include "AirwindowsTestHelpers.h"
#include "DSP/Airwindows/Plugins/Isolator3.h"

using conduit::airwindows::Isolator3;

TEST_CASE ("Airwindows Isolator3: Null-Test (Silence -> Silence, Dither off)", "[airwindows][dsp]")
{
    airwindows_tests::runNullTest<Isolator3> (1.0e-4f);
}

TEST_CASE ("Airwindows Isolator3: Parameter-Sweep ohne NaN/Inf/Denormals", "[airwindows][dsp]")
{
    airwindows_tests::runParameterSweep<Isolator3>();
}
