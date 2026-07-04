#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/Looper/LooperWaveformTap.h"
#include "Util/RtAllocationGuard.h"

using conduit::CaptureService;
using conduit::CaptureSettings;
using conduit::LooperWaveformTap;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr int    blockSize = 480;
constexpr double testBpm = 120.0;  // 1 Beat = 24 000 Samples, 1 Bin (1/32) = 750

/** Settings-Persistenz in ein Temp-Verzeichnis (Muster CaptureGateTests). */
struct TempCaptureSettings
{
    TempCaptureSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitLooperWaveformTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();

        juce::PropertiesFile::Options options;
        options.applicationName = "ConduitLooperWaveformTests";
        options.filenameSuffix  = ".settings";
        options.folderName      = folder.getFullPathName();
        settings = std::make_unique<CaptureSettings> (options);
    }

    ~TempCaptureSettings()
    {
        settings.reset();
        folder.deleteRecursively();
    }

    juce::File folder;
    std::unique_ptr<CaptureSettings> settings;
};

/** Treibt CaptureService-Tap und Waveform-Binner synchron wie der
    EngineProcessor: SampleClock und Beat-Achse laufen gemeinsam. */
struct WaveformRig
{
    WaveformRig()
    {
        service.prepare (testSampleRate, blockSize, 2);
    }

    void feed (float level, int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                for (int i = 0; i < blockSize; ++i)
                    data[i] = level;
            }

            processOneBlock();
        }
    }

    /** Stereo-Sinus mit fortlaufender Phase (Spektrum-Tests). */
    void feedSine (double freqHz, float amplitude, int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto value = amplitude * static_cast<float> (
                    std::sin (juce::MathConstants<double>::twoPi * freqHz
                              * (sinePhaseSamples + i) / testSampleRate));
                buffer.setSample (0, i, value);
                buffer.setSample (1, i, value);
            }
            sinePhaseSamples += blockSize;

            processOneBlock();
        }
    }

    /** Alle wartenden Bins abholen (Index → Bin, jüngster gewinnt). */
    std::map<std::int64_t, LooperWaveformTap::Bin> drain()
    {
        std::map<std::int64_t, LooperWaveformTap::Bin> bins;
        LooperWaveformTap::Bin bin;
        while (tap.pop (bin))
            bins[bin.index] = bin;
        return bins;
    }

    /** Alle wartenden Spektral-Spalten abholen (jüngste gewinnt). */
    std::map<std::int64_t, LooperWaveformTap::SpectralColumn> drainSpectrum()
    {
        std::map<std::int64_t, LooperWaveformTap::SpectralColumn> columns;
        LooperWaveformTap::SpectralColumn column;
        while (tap.popSpectrum (column))
            columns[column.index] = column;
        return columns;
    }

    void processOneBlock()
    {
        service.processInputTap (buffer, 2);

        conduit::ClockState clock;
        clock.bpm = testBpm;
        clock.beatAtBlockStart = beat;
        clock.sampleRate = testSampleRate;

        const auto clockNow = service.getSampleClock().now();
        tap.process (clock, service, clockNow - blockSize, blockSize);

        beat += (testBpm / (60.0 * testSampleRate)) * blockSize;
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;
    CaptureService service { *temp.settings };
    LooperWaveformTap tap;
    juce::AudioBuffer<float> buffer { 2, blockSize };
    double beat = 0.0;
    std::int64_t sinePhaseSamples = 0;
};

} // namespace

//==============================================================================
TEST_CASE ("LooperWaveformTap: konstantes Signal → exakte Min/Max-Bins, lückenlos", "[looper]")
{
    WaveformRig rig;
    rig.tap.setSource (0, 1);
    rig.service.setChannelArmed (0, true);
    rig.service.setChannelArmed (1, true);

    // Gate öffnet per Arming; der RAM-Wächter publiziert das Pool-Segment
    rig.feed (0.5f, 3);
    rig.service.runRamGuard();

    // 4 Beats Material (128 Bins à 750 Samples)
    rig.feed (0.5f, 200);

    const auto bins = rig.drain();
    REQUIRE (! bins.empty());

    // Ab Beat 1 (Bin 32) ist das Signal garantiert durchgehend im Ring —
    // jeder Bin trägt exakt den Pegel, kein Bin fehlt
    std::int64_t previous = -1;
    int checked = 0;
    for (const auto& [index, bin] : bins)
    {
        if (index < 32 || index >= 120)
            continue;

        if (previous >= 0)
            REQUIRE (index == previous + 1);  // lückenlos über Blockgrenzen
        previous = index;

        REQUIRE (juce::exactlyEqual (bin.minValue, 0.5f));
        REQUIRE (juce::exactlyEqual (bin.maxValue, 0.5f));
        ++checked;
    }

    REQUIRE (checked > 80);
}

TEST_CASE ("LooperWaveformTap: ohne lesbare Quelle kommen Null-Bins", "[looper]")
{
    WaveformRig rig;
    rig.tap.setSource (0, 1);

    // Nicht gearmt + Stille unter der Schwelle: Gate bleibt zu, der Ring
    // trägt nichts → Löcher werden als Stille gebinnt (Klassendoku)
    rig.feed (0.0f, 100);

    const auto bins = rig.drain();
    REQUIRE (! bins.empty());

    for (const auto& [index, bin] : bins)
    {
        REQUIRE (juce::exactlyEqual (bin.minValue, 0.0f));
        REQUIRE (juce::exactlyEqual (bin.maxValue, 0.0f));
    }
}

TEST_CASE ("LooperWaveformTap: Quellwechsel → Backfill der Historie, Budget pro Block", "[looper]")
{
    WaveformRig rig;

    // 4 Beats Material aufnehmen, während der Binner noch KEINE Quelle hat
    rig.service.setChannelArmed (0, true);
    rig.service.setChannelArmed (1, true);
    rig.feed (0.5f, 3);
    rig.service.runRamGuard();
    rig.feed (0.5f, 200);   // Ring: Beats ~0..4 tragen 0.5
    rig.drain();            // Null-Bins der quellenlosen Phase verwerfen

    // Quellwechsel: Reset + Backfill — die Vergangenheit wird nachgebinnt
    rig.tap.setSource (0, 1);
    rig.feed (0.5f, 1);

    // Budget-Treue: ein Block backfillt höchstens 16384 Samples an
    // Daten-Bins (~21 à 750) plus Gratis-Leerbins + Live-Bins
    const auto afterOne = rig.drain();
    int dataBins = 0;
    for (const auto& [index, bin] : afterOne)
        if (bin.maxValue > 0.0f)
            ++dataBins;
    REQUIRE (dataBins > 0);
    REQUIRE (dataBins <= 30);

    // Nach genügend Blöcken ist die sichtbare Historie vollständig: die
    // Bins der Beats 1..4 tragen wieder den Pegel
    rig.feed (0.5f, 30);
    auto all = rig.drain();
    for (const auto& [index, bin] : afterOne)
        all[index] = bin;

    int filled = 0;
    for (std::int64_t index = 32; index < 120; ++index)
    {
        const auto found = all.find (index);
        if (found != all.end() && juce::exactlyEqual (found->second.maxValue, 0.5f))
            ++filled;
    }

    REQUIRE (filled > 80);
}

TEST_CASE ("LooperWaveformTap: Mono-Quelle liest nur eine Seite", "[looper]")
{
    WaveformRig rig;
    rig.tap.setSource (0, 0);  // Mono: beide Indizes gleich
    rig.service.setChannelArmed (0, true);

    rig.feed (0.25f, 3);
    rig.service.runRamGuard();
    rig.feed (0.25f, 120);

    const auto bins = rig.drain();
    int checked = 0;
    for (const auto& [index, bin] : bins)
    {
        if (index < 32 || index >= 64)
            continue;
        REQUIRE (juce::exactlyEqual (bin.maxValue, 0.25f));
        ++checked;
    }
    REQUIRE (checked > 20);
}

TEST_CASE ("LooperWaveformTap: process ist allocation-free (RT-Audit)", "[looper]")
{
    WaveformRig rig;
    rig.tap.setSource (0, 1);
    rig.service.setChannelArmed (0, true);
    rig.service.setChannelArmed (1, true);
    rig.feed (0.5f, 3);
    rig.service.runRamGuard();
    rig.feed (0.5f, 5);

    const auto violationsBefore = conduit::rt::getAllocationViolations();

    {
        const conduit::rt::ScopedRealtimeSection rtAudit;
        rig.feed (0.5f, 64);  // inkl. Service-Tap — beide Pfade sind RT-Pflicht
    }

    if (conduit::rt::isHookActive())
        REQUIRE (conduit::rt::getAllocationViolations() == violationsBefore);
}

//==============================================================================
TEST_CASE ("LooperWaveformTap: Sinus → Energie im erwarteten Band, lückenlose Spalten", "[looper][spectrum]")
{
    WaveformRig rig;
    rig.tap.setSource (0, 1);
    rig.service.setChannelArmed (0, true);
    rig.service.setChannelArmed (1, true);

    // 1 kHz @ −6 dB — Gate öffnet per Arming, RAM-Wächter publiziert
    rig.feedSine (1000.0, 0.5f, 3);
    rig.service.runRamGuard();
    rig.feedSine (1000.0, 0.5f, 200);  // ~4 Beats = 64 Spalten

    const auto columns = rig.drainSpectrum();
    REQUIRE (! columns.empty());

    conduit::looper::SpectrumBands bands;
    bands.compute (testSampleRate);
    const auto expectedBand = bands.bandForFrequency (1000.0, testSampleRate);
    REQUIRE (expectedBand > 0);

    // Ab Beat 2 (Spalte 32) trägt jedes FFT-Fenster garantiert nur Sinus
    std::int64_t previous = -1;
    int checked = 0;
    for (const auto& [index, column] : columns)
    {
        if (index < 32 || index >= 60)
            continue;

        if (previous >= 0)
            REQUIRE (index == previous + 1);  // lückenlos über Blockgrenzen
        previous = index;

        // −6 dB minus Hann-Scalloping (≤ ~1.5 dB) → Pegel deutlich > 0.8
        REQUIRE (column.bands[(std::size_t) expectedBand] > 0.8f);

        // Entfernte Bänder bleiben leise (Hann-Sidelobes + dB-Floor)
        for (int b = 0; b < conduit::looper::spectrumBands; ++b)
            if (std::abs (b - expectedBand) >= 4)
                REQUIRE (column.bands[(std::size_t) b] < 0.5f);

        ++checked;
    }

    REQUIRE (checked > 20);
}

TEST_CASE ("LooperWaveformTap: ohne lesbare Quelle kommen Null-Spalten", "[looper][spectrum]")
{
    WaveformRig rig;
    rig.tap.setSource (0, 1);

    // Nicht gearmt + Stille: Gate bleibt zu → Loch = Stille (Klassendoku)
    rig.feed (0.0f, 100);

    const auto columns = rig.drainSpectrum();
    REQUIRE (! columns.empty());

    for (const auto& [index, column] : columns)
        for (const auto level : column.bands)
            REQUIRE (juce::exactlyEqual (level, 0.0f));
}

TEST_CASE ("LooperWaveformTap: Quellwechsel → Spektral-Backfill, Budget pro Block", "[looper][spectrum]")
{
    WaveformRig rig;

    // ~4 Beats Sinus aufnehmen, während der Binner noch KEINE Quelle hat
    rig.service.setChannelArmed (0, true);
    rig.service.setChannelArmed (1, true);
    rig.feedSine (1000.0, 0.5f, 3);
    rig.service.runRamGuard();
    rig.feedSine (1000.0, 0.5f, 200);
    rig.drainSpectrum();  // Null-Spalten der quellenlosen Phase verwerfen

    conduit::looper::SpectrumBands bands;
    bands.compute (testSampleRate);
    const auto expectedBand = bands.bandForFrequency (1000.0, testSampleRate);

    // Quellwechsel: Reset + Backfill rückwärts
    rig.tap.setSource (0, 1);
    rig.feedSine (1000.0, 0.5f, 1);

    // Budget-Treue: 32768 Samples/Block ÷ 4096 (Stereo-Fenster) = 8
    // Backfill-Spalten plus höchstens eine Live-Spalte
    const auto afterOne = rig.drainSpectrum();
    int dataColumns = 0;
    for (const auto& [index, column] : afterOne)
        if (column.bands[(std::size_t) expectedBand] > 0.5f)
            ++dataColumns;
    REQUIRE (dataColumns > 0);
    REQUIRE (dataColumns <= 10);

    // Nach genügend Blöcken ist die Historie nachgefüllt: die Spalten der
    // Beats 2..3.5 (Indizes 32..56) tragen wieder das 1-kHz-Band
    rig.feedSine (1000.0, 0.5f, 80);
    auto all = rig.drainSpectrum();
    for (const auto& [index, column] : afterOne)
        all[index] = column;

    int filled = 0;
    for (std::int64_t index = 32; index < 56; ++index)
    {
        const auto found = all.find (index);
        if (found != all.end()
            && found->second.bands[(std::size_t) expectedBand] > 0.8f)
            ++filled;
    }

    REQUIRE (filled > 20);
}
