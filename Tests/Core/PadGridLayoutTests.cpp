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

TEST_CASE ("PadGridLayout: pitchBendSemitones — Skala und Clamp", "[grid]")
{
    grid::PadGridLayout layout;

    REQUIRE (layout.pitchBendSemitones (0.3f, 0.3f) == 0.0f);

    // Eine Pad-Breite (1/cols) nach rechts -> +semitonesPerPadWidth
    const auto padWidth = 1.0f / (float) layout.cols();
    REQUIRE (layout.pitchBendSemitones (0.0f, padWidth) == 2.0f);

    // Große Bewegung -> Clamp auf +48
    REQUIRE (layout.pitchBendSemitones (0.0f, 10.0f) == 48.0f);
    REQUIRE (layout.pitchBendSemitones (10.0f, 0.0f) == -48.0f);
}

TEST_CASE ("PadGridLayout: expressionInPad — vertikale Position innerhalb des Pads", "[grid]")
{
    grid::PadGridLayout layout;

    // Oberste Reihe (padIndex 0): normY 0.0 = Pad-Oberkante -> 1.0
    REQUIRE (layout.expressionInPad (0, 0.0f) == 1.0f);

    // Unteres Ende des Pads (normY == Pad-Höhe) -> 0.0
    const auto padHeight = 1.0f / (float) layout.rows();
    REQUIRE (layout.expressionInPad (0, padHeight) == 0.0f);
}
