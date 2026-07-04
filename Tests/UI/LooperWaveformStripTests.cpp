#include <catch2/catch_test_macros.hpp>

#include "UI/LooperWaveformStrip.h"

using Bin = conduit::LooperWaveformTap::Bin;

//==============================================================================
TEST_CASE ("LooperWaveformStrip: Spalten-Aggregat über den Bin-History-Ring", "[looper][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::LooperWaveformStrip strip;
    strip.setBounds (0, 0, 400, 160);

    // 8 Takte Historie (Bins 0..1023): Bin b trägt ±(b/1024) — eindeutig
    // pro Bin, damit das Aggregat nachrechenbar ist
    for (std::int64_t b = 0; b < 1024; ++b)
    {
        const auto value = static_cast<float> (b) / 1024.0f;
        strip.ingestBinForTest ({ b, -value, value });
    }

    // beatNow = 32 (Bin-Kante 1024): der Strip zeigt exakt Bins 0..1023
    const double beatNow = 32.0;

    SECTION ("rechte Kante zeigt die jüngsten Bins")
    {
        float minValue = 0.0f, maxValue = 0.0f;
        REQUIRE (strip.aggregateColumn (399, beatNow, minValue, maxValue));

        // Spalte 399 deckt die letzten 0.04 Beats (~1–2 Bins um 1022/1023)
        REQUIRE (maxValue > 0.99f);
        REQUIRE (minValue < -0.99f);
    }

    SECTION ("linke Kante zeigt die ältesten Bins — 4× so viele pro Spalte")
    {
        float minValue = 0.0f, maxValue = 0.0f;
        REQUIRE (strip.aggregateColumn (0, beatNow, minValue, maxValue));

        // Spalte 0 deckt Beats [0, 0.16) = Bins 0..5 → Maximum ≈ 5/1024
        REQUIRE (maxValue < 0.01f);
    }

    SECTION ("Segment-Grenze: das Aggregat springt in der Dichte, nicht im Ort")
    {
        // Direkt rechts der 2-Bars/1-Bar-Grenze (x=300): Offset ~4 Beats
        // → Bins um 1024 − 128 = 896 → Werte ≈ 0.875
        float minValue = 0.0f, maxValue = 0.0f;
        REQUIRE (strip.aggregateColumn (300, beatNow, minValue, maxValue));
        REQUIRE (maxValue > 0.86f);
        REQUIRE (maxValue < 0.89f);
    }

    SECTION ("fehlende und veraltete Bins werden übersprungen")
    {
        // beatNow weit in der Zukunft: die Spalten-Bins existieren nicht
        float minValue = 0.0f, maxValue = 0.0f;
        REQUIRE_FALSE (strip.aggregateColumn (399, 5000.0, minValue, maxValue));

        // negative Beat-Achse (Session-Anfang): keine Bins < 0
        REQUIRE_FALSE (strip.aggregateColumn (0, 0.0, minValue, maxValue));
    }
}
