#include <catch2/catch_test_macros.hpp>

#include "Core/PadGridLayout.h"

namespace grid = conduit::grid;

//==============================================================================
TEST_CASE ("PadGridLayout: noteForPad — isomorphes Raster (Default-Config)", "[grid]")
{
    grid::PadGridLayout layout;

    // Unterste Reihe, Spalte 0 -> lowestNote
    REQUIRE (layout.noteForPad (24) == 48); // rows=4 -> rowFromTop 3 = unterste Reihe, cols=8 -> idx 3*8+0
    // Unterste Reihe, Spalte 1 -> +1 Halbton
    REQUIRE (layout.noteForPad (25) == 49);
    // Oberste Reihe, Spalte 0 (padIndex 0) -> lowestNote + 3*5
    REQUIRE (layout.noteForPad (0) == 48 + 3 * 5);
}

TEST_CASE ("PadGridLayout: padIndexAt — Positions-Mapping und Grenzen", "[grid]")
{
    grid::PadGridLayout layout;

    REQUIRE (layout.padIndexAt (0.0f, 0.0f) == 0);                                // oben-links
    REQUIRE (layout.padIndexAt (0.99f, 0.99f) == layout.cols() * layout.rows() - 1); // unten-rechts
    REQUIRE (layout.padIndexAt (1.5f, 0.5f) == -1);                                // außerhalb
}

TEST_CASE ("PadGridLayout: noteAt konsistent zu padIndexAt/noteForPad", "[grid]")
{
    grid::PadGridLayout layout;

    const auto x = 0.3f;
    const auto y = 0.6f;
    const auto pad = layout.padIndexAt (x, y);

    REQUIRE (pad >= 0);
    REQUIRE (layout.noteAt (x, y) == layout.noteForPad (pad));
    REQUIRE (layout.noteAt (1.5f, 0.5f) == -1);
}

TEST_CASE ("PadGridLayout: pitchBendSemitones — Skala, ungeklemmt (M1b-6)", "[grid]")
{
    grid::PadGridLayout layout;

    REQUIRE (juce::exactlyEqual (layout.pitchBendSemitones (0.3f, 0.3f), 0.0f));

    // Eine Pad-Breite (1/cols) nach rechts -> +semitonesPerPadWidth
    const auto padWidth = 1.0f / (float) layout.cols();
    REQUIRE (juce::exactlyEqual (layout.pitchBendSemitones (0.0f, padWidth), 2.0f));

    // Große Bewegung -> NICHT geklemmt, Betrag über pitchBendRangeSemitones
    // (48) hinaus (die pitchBendAxis/der Encoder klemmen erst am Ausgang).
    REQUIRE (layout.pitchBendSemitones (0.0f, 10.0f) > 48.0f);
    REQUIRE (layout.pitchBendSemitones (10.0f, 0.0f) < -48.0f);
}

TEST_CASE ("PadGridLayout: expressionFromDrag — ungeklemmter Ausdruck relativ zum Aufsetzpunkt", "[grid]")
{
    grid::PadGridLayout layout; // Default: yRangeNorm = 0.5

    // Kein Wisch -> neutral
    REQUIRE (juce::exactlyEqual (layout.expressionFromDrag (0.5f, 0.5f), 0.5f));

    // 0.25 nach oben (normY sinkt) -> volle obere Auslenkung
    REQUIRE (juce::exactlyEqual (layout.expressionFromDrag (0.5f, 0.25f), 1.0f));

    // 0.25 nach unten (normY steigt) -> volle untere Auslenkung
    REQUIRE (juce::exactlyEqual (layout.expressionFromDrag (0.5f, 0.75f), 0.0f));

    // Ungeklemmt: 0.5 nach oben/unten -> über 1 hinaus / unter 0
    REQUIRE (juce::exactlyEqual (layout.expressionFromDrag (0.5f, 0.0f), 1.5f));
    REQUIRE (juce::exactlyEqual (layout.expressionFromDrag (0.5f, 1.0f), -0.5f));
}
