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
    REQUIRE (scale::quantize (0.5f, 2, ScaleType::majorPentatonic) == Approx (0.5));

    // Skalenton bleibt liegen
    REQUIRE (scale::quantize (static_cast<float> (28.0 * semitone), 0, ScaleType::major)
             == Approx (28.0 * semitone));  // E

    // Ränder bleiben in [0, 1]
    REQUIRE (scale::quantize (1.0f, 0, ScaleType::major) <= 1.0f);
    REQUIRE (scale::quantize (0.0f, 0, ScaleType::major) >= 0.0f);
}

//==============================================================================
// Block I: Skalen-Vollausbau (26 = chromatic + 25 Ableton-/Push-Presets)

TEST_CASE ("ScaleQuantizer: alle 26 Skalen sind wohlgeformt", "[scale][blocki]")
{
    using conduit::ScaleType;
    namespace scale = conduit::scale;

    REQUIRE (scale::numScaleTypes == 26);

    for (int i = 0; i < scale::numScaleTypes; ++i)
    {
        const auto type = static_cast<ScaleType> (i);
        const auto mask = scale::maskFor (type);

        INFO ("Skala " << conduit::scaleDisplayName (type));
        REQUIRE ((mask & 1) != 0);                 // Root gehört immer dazu
        REQUIRE (mask > 0);
        REQUIRE (mask <= 0b111111111111);          // 12-Bit-Maske

        // Serialisierungs-Roundtrip über die stabile ID
        REQUIRE (conduit::scaleTypeFromString (conduit::toString (type)) == type);
        REQUIRE (conduit::scaleDisplayName (type).isNotEmpty());
    }
}

TEST_CASE ("ScaleQuantizer: Masken-Spotchecks gegen die Push-Skalenliste", "[scale][blocki]")
{
    using conduit::ScaleType;
    namespace scale = conduit::scale;

    // Dorian: 0 2 3 5 7 9 10
    REQUIRE (scale::maskFor (ScaleType::dorian) == scale::makeMask ({ 0, 2, 3, 5, 7, 9, 10 }));
    REQUIRE (scale::isInScale (9, ScaleType::dorian));
    REQUIRE_FALSE (scale::isInScale (8, ScaleType::dorian));

    // Whole Tone: nur gerade Halbtöne
    for (int semi = 0; semi < 12; ++semi)
        REQUIRE (scale::isInScale (semi, ScaleType::wholeTone) == (semi % 2 == 0));

    // Iwato: 0 1 5 6 10 (5 Töne)
    REQUIRE (juce::countNumberOfBits ((juce::uint32) scale::maskFor (ScaleType::iwato)) == 5);
    REQUIRE (scale::isInScale (6, ScaleType::iwato));
    REQUIRE_FALSE (scale::isInScale (7, ScaleType::iwato));

    // Spanish (8-Tone): 8 Töne
    REQUIRE (juce::countNumberOfBits ((juce::uint32) scale::maskFor (ScaleType::spanish)) == 8);
}

TEST_CASE ("ScaleQuantizer: Legacy-String 'pentatonic' laedt als Major Pentatonic", "[scale][blocki]")
{
    REQUIRE (conduit::scaleTypeFromString ("pentatonic") == conduit::ScaleType::majorPentatonic);
    REQUIRE (conduit::scaleTypeFromString ("unbekannt") == conduit::ScaleType::chromatic);

    // Regression gegen den Umbau: Major/Pentatonik unveraendert. Die ALTE
    // Minor-Maske (0b010101101101) hatte einen Bug -- Tritonus statt
    // Quinte ({0,2,3,5,6,8,10}); Block I fixt sie auf natuerlich Moll.
    namespace scale = conduit::scale;
    REQUIRE (scale::maskFor (conduit::ScaleType::major) == 0b101010110101);
    REQUIRE (scale::maskFor (conduit::ScaleType::minor)
             == scale::makeMask ({ 0, 2, 3, 5, 7, 8, 10 }));
    REQUIRE (scale::isInScale (7, conduit::ScaleType::minor));        // Quinte!
    REQUIRE_FALSE (scale::isInScale (6, conduit::ScaleType::minor));  // kein Tritonus
    REQUIRE (scale::maskFor (conduit::ScaleType::majorPentatonic) == 0b001010010101);
}

TEST_CASE ("ScaleQuantizer: quantize rastet in neuen Skalen korrekt", "[scale][blocki]")
{
    namespace scale = conduit::scale;

    // In-Sen ab Root 0: 0 1 5 7 10 — Halbton 3 (cv = 3/60) rastet auf 1
    // (naechster Skalenton; Gleichstand aufwaerts gilt nur bei exakter Mitte).
    const auto quantized = scale::quantize (3.0f / 60.0f, 0, conduit::ScaleType::inSen);
    const auto quantizedSemis = quantized * 60.0f;
    REQUIRE ((quantizedSemis == Approx (1.0f) || quantizedSemis == Approx (5.0f)));

    // Skalenton bleibt liegen
    REQUIRE (scale::quantize (5.0f / 60.0f, 0, conduit::ScaleType::inSen)
             == Approx (5.0f / 60.0f));
}
