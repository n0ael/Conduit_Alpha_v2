#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

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

TEST_CASE ("PadGridLayout: pitchBendSemitones — Skala, ungeklemmt (M1b-6, B3-Kalibrierung)", "[grid]")
{
    grid::PadGridLayout layout;

    REQUIRE (juce::exactlyEqual (layout.pitchBendSemitones (0.3f, 0.3f), 0.0f));

    // Eine Pad-Breite (1/cols) nach rechts -> +semitonesPerPadWidth.
    // Block B3: Default ist 1.0 (aufs isomorphe Raster kalibriert -- n
    // Spalten Wischweg = n Halbtoene; vorher 2.0, User-Befund C2->D3).
    const auto padWidth = 1.0f / (float) layout.cols();
    REQUIRE (juce::exactlyEqual (layout.pitchBendSemitones (0.0f, padWidth), 1.0f));

    // Große Bewegung -> NICHT geklemmt (die pitchBendAxis/der Encoder
    // klemmen erst am Ausgang). 10 normierte Breiten = 80 Pads = 80 HT.
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

TEST_CASE ("PadGridLayout: setYRangeNorm — groesserer Wert = kleinere Auslenkung (Block A2)", "[grid]")
{
    grid::PadGridLayout layout; // Default yRangeNorm = 0.5

    const auto before = layout.expressionFromDrag (0.5f, 0.4f);

    layout.setYRangeNorm (1.0f); // doppelt so gross -> halbe Auslenkung
    const auto afterDouble = layout.expressionFromDrag (0.5f, 0.4f);
    REQUIRE (afterDouble - 0.5f == Catch::Approx ((before - 0.5f) * 0.5f));

    layout.setYRangeNorm (0.25f); // halb so gross -> doppelte Auslenkung
    const auto afterHalf = layout.expressionFromDrag (0.5f, 0.4f);
    REQUIRE (afterHalf - 0.5f == Catch::Approx ((before - 0.5f) * 2.0f));
}

TEST_CASE ("PadGridLayout: setYRangeNorm klemmt auf > 0", "[grid]")
{
    grid::PadGridLayout layout;
    layout.setYRangeNorm (-5.0f);

    // Darf nicht durch 0/negativ teilen -- Ergebnis muss endlich sein.
    REQUIRE (std::isfinite (layout.expressionFromDrag (0.5f, 0.4f)));
}

TEST_CASE ("PadGridLayout: setSemitonesPerPadWidth skaliert pitchBendSemitones linear (Block A3)", "[grid]")
{
    grid::PadGridLayout layout;
    const auto padWidth = 1.0f / (float) layout.cols();

    layout.setSemitonesPerPadWidth (0.5f); // = Multiplikator x0.5 auf den B3-Default (1.0)
    REQUIRE (juce::exactlyEqual (layout.pitchBendSemitones (0.0f, padWidth), 0.5f));

    layout.setSemitonesPerPadWidth (4.0f); // x4
    REQUIRE (juce::exactlyEqual (layout.pitchBendSemitones (0.0f, padWidth), 4.0f));

    layout.setSemitonesPerPadWidth (16.0f); // jenseits der A3-Stufen -- Setter bleibt generisch
    REQUIRE (juce::exactlyEqual (layout.pitchBendSemitones (0.0f, padWidth), 16.0f));
}

//==============================================================================
// In-Tune-Staircase (Block B1/B2)

TEST_CASE ("PadGridLayout: pitchBendFromAnchor -- Zone 0 = exakt linear", "[grid]")
{
    grid::PadGridLayout::Config config;
    config.inTuneWidthPercent = 0.0f;
    grid::PadGridLayout layout (config);

    const auto padWidth = 1.0f / (float) layout.cols();

    for (const auto d : { -2.0f, -0.7f, -0.25f, 0.0f, 0.3f, 1.0f, 3.5f })
        REQUIRE (layout.pitchBendFromAnchor (0.5f, 0.5f + d * padWidth)
                 == Catch::Approx (layout.pitchBendSemitones (0.5f, 0.5f + d * padWidth)).margin (1.0e-4));
}

TEST_CASE ("PadGridLayout: pitchBendFromAnchor -- In-Tune-Zone haelt 0, Nachbar-Zentrum = 1 HT", "[grid]")
{
    grid::PadGridLayout::Config config;
    config.inTuneWidthPercent = 50.0f;
    grid::PadGridLayout layout (config);

    const auto padWidth = 1.0f / (float) layout.cols();
    const auto anchor = 0.5f;

    // Innerhalb der Zone (Breite 0.5 Pad-Breiten um den Anker): 0 Bend
    REQUIRE (juce::exactlyEqual (layout.pitchBendFromAnchor (anchor, anchor), 0.0f));
    REQUIRE (juce::exactlyEqual (layout.pitchBendFromAnchor (anchor, anchor + 0.2f * padWidth), 0.0f));
    REQUIRE (juce::exactlyEqual (layout.pitchBendFromAnchor (anchor, anchor - 0.2f * padWidth), 0.0f));

    // Naechstes Pad-Zentrum (+-1 Pad-Breite): exakt +-1 HT (B3-Default 1.0)
    REQUIRE (layout.pitchBendFromAnchor (anchor, anchor + padWidth) == Catch::Approx (1.0f));
    REQUIRE (layout.pitchBendFromAnchor (anchor, anchor - padWidth) == Catch::Approx (-1.0f));

    // Auch weiter entfernte Zentren rasten exakt (+3 Pads = +3 HT)
    REQUIRE (layout.pitchBendFromAnchor (anchor, anchor + 3.0f * padWidth) == Catch::Approx (3.0f));

    // Zwischen den Zonen laeuft der Bend steiler, bleibt aber monoton
    const auto quarter = layout.pitchBendFromAnchor (anchor, anchor + 0.4f * padWidth);
    const auto half    = layout.pitchBendFromAnchor (anchor, anchor + 0.5f * padWidth);
    const auto edge    = layout.pitchBendFromAnchor (anchor, anchor + 0.75f * padWidth);
    REQUIRE (quarter > 0.0f);
    REQUIRE (half > quarter);
    REQUIRE (edge > half);
    REQUIRE (edge == Catch::Approx (1.0f));   // 0.75 = 1 - Zone/2 -> Rand der Nachbar-Zone
}

TEST_CASE ("PadGridLayout: pitchBendFromAnchor ist stetig am Zonenrand", "[grid]")
{
    grid::PadGridLayout::Config config;
    config.inTuneWidthPercent = 20.0f;   // Default
    grid::PadGridLayout layout (config);

    const auto padWidth = 1.0f / (float) layout.cols();
    const auto anchor = 0.5f;

    // Knapp innerhalb/ausserhalb des Zonenrands (0.1 Pad-Breiten) duerfen
    // sich nur minimal unterscheiden (Stetigkeit der Treppen-Kennlinie).
    const auto inside  = layout.pitchBendFromAnchor (anchor, anchor + 0.099f * padWidth);
    const auto outside = layout.pitchBendFromAnchor (anchor, anchor + 0.101f * padWidth);
    REQUIRE (juce::exactlyEqual (inside, 0.0f));
    REQUIRE (outside < 0.01f);
}

//==============================================================================
// Block D1: setInTuneWidthPercent / setLowestNote

TEST_CASE ("PadGridLayout: setInTuneWidthPercent klemmt auf [0,95] und wirkt sofort", "[grid]")
{
    grid::PadGridLayout layout;
    const auto padWidth = 1.0f / (float) layout.cols();

    layout.setInTuneWidthPercent (150.0f);   // ueber 95 geklemmt
    // Bei einer sehr breiten Zone bleibt selbst 0.4 Pad-Breiten Abstand
    // noch (nahe) 0 -- Regressionsschutz gegen ungeklemmtes >100%.
    REQUIRE (layout.pitchBendFromAnchor (0.5f, 0.5f + 0.4f * padWidth) < 0.5f);

    layout.setInTuneWidthPercent (-10.0f);   // unter 0 geklemmt -> exakt linear
    REQUIRE (layout.pitchBendFromAnchor (0.5f, 0.5f + 0.4f * padWidth)
             == Catch::Approx (layout.pitchBendSemitones (0.5f, 0.5f + 0.4f * padWidth)).margin (1.0e-4));
}

TEST_CASE ("PadGridLayout: setLowestNote setzt den Grundton absolut, geklemmt [0,127]", "[grid]")
{
    grid::PadGridLayout layout;   // Default lowestNote 48

    REQUIRE (layout.lowestNote() == 48);
    REQUIRE (layout.noteForPad (24) == 48);   // unterste Reihe, Spalte 0

    layout.setLowestNote (60);   // +1 Oktave
    REQUIRE (layout.lowestNote() == 60);
    REQUIRE (layout.noteForPad (24) == 60);

    layout.setLowestNote (200);   // geklemmt auf 127
    REQUIRE (layout.lowestNote() == 127);

    layout.setLowestNote (-5);   // geklemmt auf 0
    REQUIRE (layout.lowestNote() == 0);
}
