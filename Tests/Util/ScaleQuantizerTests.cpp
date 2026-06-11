#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Util/ScaleQuantizer.h"

using Catch::Approx;
using conduit::ScaleType;
namespace scale = conduit::scale;

namespace
{
constexpr auto semitone = 1.0 / scale::semitonesPerUnit;
}

//==============================================================================
TEST_CASE ("ScaleQuantizer: Skalen-Masken", "[scale]")
{
    // C-Dur: C D E F G A B
    for (const auto inScale : { 0, 2, 4, 5, 7, 9, 11 })
        REQUIRE (scale::isInScale (inScale, ScaleType::major));

    for (const auto outOfScale : { 1, 3, 6, 8, 10 })
        REQUIRE_FALSE (scale::isInScale (outOfScale, ScaleType::major));

    // Oktav-Wrap und negative Halbtöne (unter dem Grundton)
    REQUIRE (scale::isInScale (12, ScaleType::major));
    REQUIRE (scale::isInScale (-12, ScaleType::major));
    REQUIRE (scale::isInScale (-5, ScaleType::major));        // = 7 (G)
    REQUIRE_FALSE (scale::isInScale (-6, ScaleType::major));  // = 6 (F#)
}

//==============================================================================
TEST_CASE ("ScaleQuantizer: quantize rastet auf den nächsten Skalenton", "[scale]")
{
    // Chromatisch: rastet auf Halbtöne (Urzwerg-Verhalten)
    REQUIRE (scale::quantize (0.5f, 0, ScaleType::chromatic) == Approx (0.5));          // 30 Halbtöne exakt
    REQUIRE (scale::quantize (static_cast<float> (30.4 * semitone), 0, ScaleType::chromatic)
             == Approx (30.0 * semitone));

    // C-Dur: 0.5 = F# (30 Halbtöne) — Gleichstand F/G → aufwärts zu G (31)
    REQUIRE (scale::quantize (0.5f, 0, ScaleType::major) == Approx (31.0 * semitone));

    // c-Moll-Grundton verschiebt die Maske: F# über D-Grundton = 4 → in D-Pentatonik
    REQUIRE (scale::quantize (0.5f, 2, ScaleType::pentatonic) == Approx (0.5));

    // Skalenton bleibt liegen
    REQUIRE (scale::quantize (static_cast<float> (28.0 * semitone), 0, ScaleType::major)
             == Approx (28.0 * semitone));  // E

    // Ränder bleiben in [0, 1]
    REQUIRE (scale::quantize (1.0f, 0, ScaleType::major) <= 1.0f);
    REQUIRE (scale::quantize (0.0f, 0, ScaleType::major) >= 0.0f);
}
