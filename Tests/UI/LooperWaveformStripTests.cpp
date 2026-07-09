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

//==============================================================================
TEST_CASE ("LooperWaveformStrip: Spektrum-Ring-Image, Stale-Clear und View (S2)", "[looper][ui][spectrum]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::LooperWaveformStrip strip;
    strip.setBounds (0, 0, 400, 160);

    using Strip = conduit::LooperWaveformStrip;
    using Column = conduit::LooperWaveformTap::SpectralColumn;
    constexpr auto ring = Strip::spectrumRingColumns;

    SECTION ("Spalte landet an der Ringposition, Pegel färbt heiß")
    {
        Column column;
        column.index = 5;
        column.bands[10] = 1.0f;   // volles Band → hellstes LUT-Ende
        strip.ingestSpectrumForTest (column);

        REQUIRE (strip.getSpectrumTagForTest (5) == 5);

        const auto hot  = strip.getSpectrumPixelForTest (5, 10);
        const auto cold = strip.getSpectrumPixelForTest (5, 40);   // Pegel 0
        REQUIRE (hot.getPerceivedBrightness() > 0.7f);
        REQUIRE (cold.getPerceivedBrightness() < 0.3f);
        REQUIRE (hot != cold);

        // Ring-Wrap: Index ring+5 überschreibt denselben Slot mit neuem Tag
        Column wrapped;
        wrapped.index = ring + 5;
        strip.ingestSpectrumForTest (wrapped);
        REQUIRE (strip.getSpectrumTagForTest (5) == ring + 5);
        REQUIRE (strip.getSpectrumPixelForTest (5, 10)
                 .getPerceivedBrightness() < 0.3f);  // Null-Spalte überschrieb
    }

    SECTION ("Stale-Clear schwärzt veraltete Slots im sichtbaren Fenster")
    {
        // Slot 5 trägt eine URALTE Spalte (Index 5); sichtbar wäre dort
        // aber Index ring+5 → der Tag passt nicht und wird geräumt
        Column stale;
        stale.index = 5;
        stale.bands[10] = 1.0f;
        strip.ingestSpectrumForTest (stale);

        // beatNow so, dass Index ring+5 im sichtbaren Fenster liegt
        strip.setBeatNowForTest (static_cast<double> (ring + 6)
                                 / conduit::looper::spectrumColumnsPerBeat);
        strip.clearStaleSpectrumColumnsForTest();

        REQUIRE (strip.getSpectrumTagForTest (5) == -1);
        REQUIRE (strip.getSpectrumPixelForTest (5, 10).getPerceivedBrightness() < 0.3f);

        // Passende Tags bleiben unangetastet
        Column fresh;
        fresh.index = ring + 5;
        fresh.bands[10] = 1.0f;
        strip.ingestSpectrumForTest (fresh);
        strip.clearStaleSpectrumColumnsForTest();
        REQUIRE (strip.getSpectrumTagForTest (5) == ring + 5);
        REQUIRE (strip.getSpectrumPixelForTest (5, 10).getPerceivedBrightness() > 0.7f);
    }

    SECTION ("View-Umschaltung: default Wellenform, Setter wechselt")
    {
        REQUIRE (strip.getView() == Strip::View::waveform);
        strip.setView (Strip::View::spectrum);
        REQUIRE (strip.getView() == Strip::View::spectrum);
        strip.setView (Strip::View::waveform);
        REQUIRE (strip.getView() == Strip::View::waveform);
    }
}

//==============================================================================
TEST_CASE ("LooperWaveformStrip: Commit-Thumbnail als Tinte auf transparent", "[looper][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::LooperWaveformStrip strip;
    strip.setBounds (0, 0, 400, 160);

    using Strip = conduit::LooperWaveformStrip;
    using Column = conduit::LooperWaveformTap::SpectralColumn;

    SECTION ("Waveform: Signal-Spalten tragen schwarze Tinte, Leere bleibt transparent")
    {
        // Nur der Takt [4, 8) Beats trägt Vollpegel (Bins 128..255,
        // binsPerBeat 32) — die erste Fensterhälfte bleibt leer
        for (std::int64_t b = 128; b < 256; ++b)
            strip.ingestBinForTest ({ b, -1.0f, 1.0f });

        const auto image = strip.renderCommitThumbnail (0.0, 8.0, 128, 32);
        REQUIRE (image.getWidth() == 128);
        REQUIRE (image.getHeight() == 32);

        // Spalte 32 → Beats um 2.0 (leer); Spalte 96 → Beats um 6.0 (voll)
        REQUIRE (image.getPixelAt (32, 16).getAlpha() == 0);

        const auto ink = image.getPixelAt (96, 16);
        REQUIRE (ink.getAlpha() > 200);
        REQUIRE (ink.getPerceivedBrightness() < 0.1f);   // Tinte ist Schwarz
    }

    SECTION ("Waveform: kleiner Pegel malt nur um die Mittellinie")
    {
        for (std::int64_t b = 0; b < 256; ++b)
            strip.ingestBinForTest ({ b, -0.05f, 0.05f });

        const auto image = strip.renderCommitThumbnail (0.0, 8.0, 128, 32);
        REQUIRE (image.getPixelAt (64, 16).getAlpha() > 0);
        REQUIRE (image.getPixelAt (64, 2).getAlpha() == 0);
        REQUIRE (image.getPixelAt (64, 29).getAlpha() == 0);
    }

    SECTION ("Spektrum-View: Band-Intensität wird Tinten-Alpha, Lücken transparent")
    {
        strip.setView (Strip::View::spectrum);

        Column column;
        column.index = 8;          // Beat-Fenster [0,1) → Spaltenindizes 0..15
        column.bands[10] = 1.0f;   // ein heißes Band
        strip.ingestSpectrumForTest (column);

        // Bild 16×64: Spalte x trifft exakt Spaltenindex x, Zeile y = Ring-
        // Zeile y (Band 10 liegt in Zeile 63 − 10 = 53, Spektrogramm-Konvention)
        const auto image = strip.renderCommitThumbnail (0.0, 1.0, 16, 64);

        REQUIRE (image.getPixelAt (8, 53).getAlpha() > 128);           // heißes Band
        REQUIRE (image.getPixelAt (8, 63 - 40).getAlpha() == 0);       // stilles Band
        REQUIRE (image.getPixelAt (3, 53).getAlpha() == 0);            // Spalte ohne Daten
    }

    SECTION ("leeres Fenster liefert ein gültiges, transparentes Bild")
    {
        const auto image = strip.renderCommitThumbnail (8.0, 8.0, 64, 32);
        REQUIRE (image.isValid());
        REQUIRE (image.getPixelAt (32, 16).getAlpha() == 0);
    }
}
