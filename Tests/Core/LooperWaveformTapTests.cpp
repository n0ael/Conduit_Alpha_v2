#include <algorithm>
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

            service.processInputTap (buffer, 2);

            conduit::ClockState clock;
            clock.bpm = testBpm;
            clock.beatAtBlockStart = beat;
            clock.sampleRate = testSampleRate;

            const auto clockNow = service.getSampleClock().now();
            tap.process (clock, service, clockNow - blockSize, blockSize);

            beat += (testBpm / (60.0 * testSampleRate)) * blockSize;
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

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;
    CaptureService service { *temp.settings };
    LooperWaveformTap tap;
    juce::AudioBuffer<float> buffer { 2, blockSize };
    double beat = 0.0;
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
