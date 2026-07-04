#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/Looper/LooperMath.h"

using Catch::Approx;
namespace lm = conduit::looper;

//==============================================================================
TEST_CASE ("LooperMath: Segment-Konstanten des Endlesss-Layouts", "[looper]")
{
    // Labels links → rechts: 8 | 4 | 2 | 1 Takte
    REQUIRE (lm::barsForSegment (0) == 8);
    REQUIRE (lm::barsForSegment (1) == 4);
    REQUIRE (lm::barsForSegment (2) == 2);
    REQUIRE (lm::barsForSegment (3) == 1);
    REQUIRE (lm::barsForSegment (-1) == 0);
    REQUIRE (lm::barsForSegment (4) == 0);

    // Rechte Kanten (Beat-Offset rückwärts): 16 | 8 | 4 | 0
    REQUIRE (lm::segmentRightEdgeBeats (0) == Approx (16.0));
    REQUIRE (lm::segmentRightEdgeBeats (1) == Approx (8.0));
    REQUIRE (lm::segmentRightEdgeBeats (2) == Approx (4.0));
    REQUIRE (lm::segmentRightEdgeBeats (3) == Approx (0.0));

    // Spannen: 16 | 8 | 4 | 4 — die Dichte verdoppelt sich an den
    // Grenzen 2→1 und 1→0, Segment 3 und 2 zeigen je einen ganzen Takt
    REQUIRE (lm::segmentSpanBeats (0) == Approx (16.0));
    REQUIRE (lm::segmentSpanBeats (1) == Approx (8.0));
    REQUIRE (lm::segmentSpanBeats (2) == Approx (4.0));
    REQUIRE (lm::segmentSpanBeats (3) == Approx (4.0));

    // Gesamt-Sichtfenster: 8 Takte = 32 Beats
    REQUIRE (lm::segmentRightEdgeBeats (0) + lm::segmentSpanBeats (0)
             == Approx (lm::maxBars * lm::quantumBeats));
}

TEST_CASE ("LooperMath: Pixel → Segment und Pixel → Beat-Offset", "[looper]")
{
    constexpr double width = 400.0;

    SECTION ("segmentForX: Viertel-Aufteilung mit Clamp")
    {
        REQUIRE (lm::segmentForX (0.0, width) == 0);
        REQUIRE (lm::segmentForX (99.9, width) == 0);
        REQUIRE (lm::segmentForX (100.0, width) == 1);
        REQUIRE (lm::segmentForX (250.0, width) == 2);
        REQUIRE (lm::segmentForX (399.9, width) == 3);

        // Clamp: außerhalb des Strips und degenerierte Breite
        REQUIRE (lm::segmentForX (-5.0, width) == 0);
        REQUIRE (lm::segmentForX (400.0, width) == 3);
        REQUIRE (lm::segmentForX (100.0, 0.0) == 3);
    }

    SECTION ("beatOffsetForX: Kanten und Segment-Mitten")
    {
        // Rechte Strip-Kante = jetzt, linke = 8 Takte zurück
        REQUIRE (lm::beatOffsetForX (width, width) == Approx (0.0));
        REQUIRE (lm::beatOffsetForX (0.0, width) == Approx (32.0));

        // Segment-Mitten: 3 → 2 Beats, 2 → 6, 1 → 12, 0 → 24
        REQUIRE (lm::beatOffsetForX (350.0, width) == Approx (2.0));
        REQUIRE (lm::beatOffsetForX (250.0, width) == Approx (6.0));
        REQUIRE (lm::beatOffsetForX (150.0, width) == Approx (12.0));
        REQUIRE (lm::beatOffsetForX (50.0, width) == Approx (24.0));
    }

    SECTION ("beatOffsetForX ist an den Segment-Grenzen stetig")
    {
        // Beide Seiten jeder Grenze müssen denselben Offset liefern —
        // nur die DICHTE springt, nicht die Position
        for (double edge : { 100.0, 200.0, 300.0 })
        {
            const auto below = lm::beatOffsetForX (edge - 1.0e-9, width);
            const auto above = lm::beatOffsetForX (edge, width);
            REQUIRE (below == Approx (above).margin (1.0e-6));
        }

        REQUIRE (lm::beatOffsetForX (300.0, width) == Approx (4.0));
        REQUIRE (lm::beatOffsetForX (200.0, width) == Approx (8.0));
        REQUIRE (lm::beatOffsetForX (100.0, width) == Approx (16.0));
    }

    SECTION ("Dichte-Verdopplung: Beats pro Pixel je Segment")
    {
        const auto density = [&] (double x)
        {
            return lm::beatOffsetForX (x, width) - lm::beatOffsetForX (x + 1.0, width);
        };

        REQUIRE (density (350.0) == Approx (4.0 / 100.0));   // Segment 3
        REQUIRE (density (250.0) == Approx (4.0 / 100.0));   // Segment 2 (gleich)
        REQUIRE (density (150.0) == Approx (8.0 / 100.0));   // Segment 1 (2×)
        REQUIRE (density (50.0) == Approx (16.0 / 100.0));   // Segment 0 (4×)
    }
}

//==============================================================================
TEST_CASE ("LooperMath: commitRangeForBars — letzte N komplette Takte", "[looper]")
{
    // Jüngste Grenze 9 → Grenzen 1..9 geankert, 8 Takte adressierbar
    const auto full = lm::commitRangeForBars (9, 8);
    REQUIRE (full.valid);
    REQUIRE (full.startBar == 1);
    REQUIRE (full.endBar == 9);
    REQUIRE (full.lengthBeats() == Approx (32.0));
    REQUIRE (full.endBeat() == Approx (36.0));

    const auto one = lm::commitRangeForBars (12, 1);
    REQUIRE (one.valid);
    REQUIRE (one.startBar == 11);
    REQUIRE (one.endBar == 12);
    REQUIRE (one.lengthBeats() == Approx (4.0));

    // Session-Anfang: Grenze 0 wird nie geankert → bars+1 Grenzen nötig
    REQUIRE_FALSE (lm::commitRangeForBars (8, 8).valid);
    REQUIRE_FALSE (lm::commitRangeForBars (1, 1).valid);
    REQUIRE (lm::commitRangeForBars (2, 1).valid);
    REQUIRE_FALSE (lm::commitRangeForBars (0, 1).valid);
    REQUIRE_FALSE (lm::commitRangeForBars (-1, 1).valid);

    // Kanten der Commit-Länge
    REQUIRE_FALSE (lm::commitRangeForBars (20, 0).valid);
    REQUIRE_FALSE (lm::commitRangeForBars (20, 9).valid);
}

TEST_CASE ("LooperMath: loopPhaseBeats — beat-abgeleitete Phase ohne Drift", "[looper]")
{
    // Loop [B−L, B) mit B = 32, L = 4: an der Commit-Grenze beginnt Phase 0
    REQUIRE (lm::loopPhaseBeats (32.0, 32.0, 4.0) == Approx (0.0));
    REQUIRE (lm::loopPhaseBeats (33.5, 32.0, 4.0) == Approx (1.5));
    REQUIRE (lm::loopPhaseBeats (36.0, 32.0, 4.0) == Approx (0.0));   // Wrap
    REQUIRE (lm::loopPhaseBeats (37.25, 32.0, 4.0) == Approx (1.25));

    // Session-Beat VOR der Grenze (Beat-Sprung rückwärts): positiv gewrappt
    REQUIRE (lm::loopPhaseBeats (31.0, 32.0, 4.0) == Approx (3.0));

    // Degenerierte Länge fällt definiert auf 0
    REQUIRE (lm::loopPhaseBeats (10.0, 8.0, 0.0) == Approx (0.0));
    REQUIRE (lm::loopPhaseBeats (10.0, 8.0, -4.0) == Approx (0.0));
}

//==============================================================================
TEST_CASE ("LooperMath: SpectrumBands — log-Bänder, strikt monoton, DC-frei", "[looper][spectrum]")
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        lm::SpectrumBands bands;
        bands.compute (sampleRate);

        const auto nyquistBin = lm::spectrumFftSize / 2;

        // DC bleibt außen vor, letzte Grenze ≤ Nyquist-Bin
        REQUIRE (bands.edges.front() >= 1);
        REQUIRE (bands.edges.back() <= nyquistBin);

        // Strikt monoton → jedes Band trägt mindestens einen Bin,
        // lückenlos (Band b endet, wo b+1 beginnt)
        for (int b = 0; b < lm::spectrumBands; ++b)
            REQUIRE (bands.edges[(std::size_t) b + 1] > bands.edges[(std::size_t) b]);

        // Referenzfrequenzen liegen in genau einem Band, aufsteigend
        const auto low  = bands.bandForFrequency (100.0, sampleRate);
        const auto mid  = bands.bandForFrequency (1000.0, sampleRate);
        const auto high = bands.bandForFrequency (10000.0, sampleRate);
        REQUIRE (low >= 0);
        REQUIRE (mid > low);
        REQUIRE (high > mid);
        REQUIRE (high < lm::spectrumBands);
    }
}

TEST_CASE ("LooperMath: spectrumLevel — dB-Mapping mit Floor und Clamp", "[looper][spectrum]")
{
    const auto fullScale = static_cast<float> (lm::spectrumFftSize) * 0.25f;

    // Full-Scale-Sinus (Hann-Referenz) = 0 dB → 1.0; Übersteuerung clampt
    REQUIRE (lm::spectrumLevel (fullScale) == Approx (1.0f));
    REQUIRE (lm::spectrumLevel (fullScale * 4.0f) == Approx (1.0f));

    // −66 dB = Floor → 0; Stille → 0
    REQUIRE (lm::spectrumLevel (fullScale * std::pow (10.0f, -66.0f / 20.0f))
             == Approx (0.0f).margin (1.0e-4));
    REQUIRE (lm::spectrumLevel (0.0f) == Approx (0.0f));

    // −6 dB (Amplitude 0.5) ≈ 0.909, monoton dazwischen
    REQUIRE (lm::spectrumLevel (fullScale * 0.5f) == Approx (0.9088f).margin (1.0e-3));
    REQUIRE (lm::spectrumLevel (fullScale * 0.5f) > lm::spectrumLevel (fullScale * 0.1f));
}
