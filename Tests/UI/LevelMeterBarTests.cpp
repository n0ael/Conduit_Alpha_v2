#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "UI/LevelMeterBar.h"

using Catch::Approx;
using conduit::LevelMeterBar;

//==============================================================================
// dBFS-Mapping der Pegelanzeige: linearer Gain → 0..1 über −60…0 dB.
//==============================================================================
TEST_CASE ("LevelMeterBar::normFromLinear mappt dBFS auf 0..1", "[ui][io]")
{
    SECTION ("0 dBFS (Vollaussteuerung) → 1.0")
        REQUIRE (LevelMeterBar::normFromLinear (1.0f) == Approx (1.0f));

    SECTION ("Stille → 0")
    {
        REQUIRE (LevelMeterBar::normFromLinear (0.0f) == Approx (0.0f));
        REQUIRE (LevelMeterBar::normFromLinear (-0.5f) == Approx (0.0f));  // negativ = Stille
    }

    SECTION ("−6 dB (~0.5 linear) → ~0.9")
        REQUIRE (LevelMeterBar::normFromLinear (0.5f) == Approx (0.9f).margin (0.01));

    SECTION ("−60 dB (Skalenboden) → 0")
        REQUIRE (LevelMeterBar::normFromLinear (0.001f) == Approx (0.0f).margin (0.001));

    SECTION ("unter −60 dB wird auf 0 geklemmt")
        REQUIRE (LevelMeterBar::normFromLinear (0.0001f) == Approx (0.0f));

    SECTION ("über 0 dBFS (Clip) wird auf 1.0 geklemmt")
        REQUIRE (LevelMeterBar::normFromLinear (2.0f) == Approx (1.0f));

    SECTION ("monoton steigend")
    {
        REQUIRE (LevelMeterBar::normFromLinear (0.1f) < LevelMeterBar::normFromLinear (0.3f));
        REQUIRE (LevelMeterBar::normFromLinear (0.3f) < LevelMeterBar::normFromLinear (0.7f));
    }
}
