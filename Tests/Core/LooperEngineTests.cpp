#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "Core/Looper/BarSampleAnchors.h"
#include "Core/Looper/LooperEngine.h"
#include "Util/RtAllocationGuard.h"

using conduit::BarSampleAnchors;
using conduit::CaptureService;
using conduit::CaptureSettings;
using conduit::LooperEngine;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr int    blockSize = 480;
// 120 BPM @ 48 kHz: 1 Beat = 24 000 Samples, 1 Takt = 96 000 Samples

struct TempCaptureSettings
{
    TempCaptureSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitLooperEngineTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();

        juce::PropertiesFile::Options options;
        options.applicationName = "ConduitLooperEngineTests";
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

/** Audio-Callback-Surrogat: Input-Tap → Takt-Anker → Loop-Playback laufen
    synchron wie im EngineProcessor; das Eingangssignal ist eine Funktion
    der ABSOLUTEN SampleClock-Position (Loop-Inhalt nachrechenbar). */
struct EngineRig
{
    EngineRig()
    {
        service.prepare (testSampleRate, blockSize, 2);
        engine.prepare (testSampleRate);
        service.setChannelArmed (0, true);
        service.setChannelArmed (1, true);

        // Gate öffnet per Arming, der RAM-Wächter publiziert das Segment
        feedBlocks (3);
        service.runRamGuard();
    }

    void feedBlocks (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            const auto blockStart = service.getSampleClock().now();

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* data = input.getWritePointer (ch);
                for (int i = 0; i < blockSize; ++i)
                    data[i] = signal != nullptr
                            ? signal (blockStart + static_cast<std::uint64_t> (i))
                            : 0.5f;  // Default hält das Gate-Material lesbar
            }

            service.processInputTap (input, 2);

            conduit::ClockState clock;
            clock.bpm = bpm;
            clock.beatAtBlockStart = beat + nextClockJitterBeats();
            clock.sampleRate = testSampleRate;

            anchors.process (clock, blockStart, blockSize);

            output.clear();
            engine.process (output, 2, clock, blockStart, anchors);

            beat += clock.beatsPerSample() * blockSize;
        }
    }

    void feedBars (double bars) { feedBlocks ((int) std::lround (bars * 4.0 * 24000.0 / blockSize)); }

    /** Wall-Clock-Jitter des realen captureClockState: bipolares LCG-
        Rauschen ±clockJitterBeats auf dem Block-Beat (0 = jitter-frei). */
    float nextClockJitterBeats() noexcept
    {
        if (clockJitterBeats <= 0.0)
            return 0.0f;

        jitterState = 1664525u * jitterState + 1013904223u;
        const auto unit = static_cast<double> (jitterState) / 4294967295.0;  // 0..1
        return static_cast<float> ((unit * 2.0 - 1.0) * clockJitterBeats);
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;
    CaptureService service { *temp.settings };
    BarSampleAnchors anchors;
    LooperEngine engine;
    juce::AudioBuffer<float> input  { 2, blockSize };
    juce::AudioBuffer<float> output { 2, blockSize };
    double beat = 0.0;
    double bpm = 120.0;
    double clockJitterBeats = 0.0;
    std::uint32_t jitterState = 0x9e3779b9u;
    std::function<float (std::uint64_t)> signal;
};

} // namespace

//==============================================================================
TEST_CASE ("LooperEngine: Commit ist sample-exakt und phasenstarr zum Session-Beat", "[looper]")
{
    EngineRig rig;

    // Sägezahn mit Perioden-Länge 1 Beat, phasenstarr zur SampleClock:
    // an Taktgrenzen (Vielfache von 96 000) startet die Periode bei 0 —
    // der erwartete Loop-Output ist damit 0.5·frac(Phase in Beats)
    rig.signal = [] (std::uint64_t pos)
    { return 0.5f * static_cast<float> (pos % 24000) / 24000.0f; };

    rig.feedBars (2.5);
    REQUIRE (rig.anchors.latestBoundaryBar() >= 2);

    REQUIRE (rig.engine.commit (1, rig.service, 0, 1, rig.anchors).wasOk());
    REQUIRE (rig.engine.isPlaying());
    REQUIRE (rig.engine.getLoopBars() == 1);

    // Fade-In (240 Samples) ausklingen lassen, dann prüfen
    rig.feedBlocks (2);

    int checked = 0;
    for (int block = 0; block < 40; ++block)
    {
        const auto blockStartBeat = rig.beat;
        rig.feedBlocks (1);

        for (int i = 0; i < blockSize; i += 7)
        {
            const auto beatAt = blockStartBeat + (120.0 / (60.0 * testSampleRate)) * i;

            // Erwartung: Loop-Länge 4 Beats, Ende an einer Taktgrenze
            // (endBeat ≡ 0 mod 4) → Output = 0.5·frac(beat), außerhalb
            // von Perioden-Sprung und Wrap-Zone
            const auto fracBeat = beatAt - std::floor (beatAt);
            const auto beatInBar = beatAt - std::floor (beatAt / 4.0) * 4.0;
            if (fracBeat < 0.05 || fracBeat > 0.95)
                continue;  // Sägezahn-Sprung (Interpolation)
            if (beatInBar > 3.98)
                continue;  // Wrap-Zone (Crossfade auf den Lead-in)

            const auto expected = 0.5f * static_cast<float> (fracBeat);
            const auto actual = rig.output.getSample (0, i);
            REQUIRE (std::abs (actual - expected) < 5.0e-3f);
            ++checked;
        }
    }

    REQUIRE (checked > 1'500);
}

TEST_CASE ("LooperEngine: Wrap-Crossfade hält den Übergang stetig", "[looper]")
{
    EngineRig rig;

    // Periode bewusst NICHT bar-synchron: am Wrap springt das Material um
    // ~0.58 — ohne Crossfade wäre der Sprung 1:1 hörbar
    rig.signal = [] (std::uint64_t pos)
    {
        return 0.8f * static_cast<float> (
            std::sin (juce::MathConstants<double>::twoPi
                      * static_cast<double> (pos) / 70000.0));
    };

    rig.feedBars (2.5);
    REQUIRE (rig.engine.commit (1, rig.service, 0, 1, rig.anchors).wasOk());
    rig.feedBlocks (2);  // Fade-In vorbei

    // Über mehrere Loop-Durchläufe: maximale Sample-zu-Sample-Differenz —
    // deckt insbesondere die Wraps ab (alle 96 000 Samples = 200 Blöcke)
    float previous = 0.0f;
    bool havePrevious = false;
    float maxDelta = 0.0f;

    for (int block = 0; block < 450; ++block)
    {
        rig.feedBlocks (1);
        for (int i = 0; i < blockSize; ++i)
        {
            const auto sample = rig.output.getSample (0, i);
            if (havePrevious)
                maxDelta = juce::jmax (maxDelta, std::abs (sample - previous));
            previous = sample;
            havePrevious = true;
        }
    }

    // Signal-Steigung ≈ 2π·0.8/70000·1 ≈ 7e-5/Sample; der 0.58-Sprung über
    // 240 Crossfade-Samples ≈ 2.4e-3 + Blende — alles weit unter 0.05
    REQUIRE (maxDelta < 0.05f);
}

TEST_CASE ("LooperEngine: Wall-Clock-Jitter erreicht den Lesekopf nicht", "[looper]")
{
    EngineRig rig;

    // Reales Symptom (B5-Ohr-Abnahme 07/2026): beatAtBlockStart stammt aus
    // der Link-Wall-Clock und jittert um den Callback-Scheduling-Versatz —
    // als direkte Lese-Basis sprang der Lesekopf pro Block um Dutzende
    // Samples (körnige "falsche Samplerate"-Verzerrung). ±0.002 Beats
    // ≈ ±1 ms ≈ ±48 Samples @ 120 BPM / 48 kHz.
    rig.clockJitterBeats = 0.002;

    // 100-Hz-Sinus (Periode 480 Samples): steil genug, dass Lesekopf-
    // Sprünge als große Sample-Deltas sichtbar würden (max. Steigung
    // ≈ 2π·0.8/480 ≈ 0.0105/Sample)
    rig.signal = [] (std::uint64_t pos)
    {
        return 0.8f * static_cast<float> (
            std::sin (juce::MathConstants<double>::twoPi
                      * static_cast<double> (pos) / 480.0));
    };

    rig.feedBars (2.5);
    REQUIRE (rig.engine.commit (1, rig.service, 0, 1, rig.anchors).wasOk());
    rig.feedBlocks (2);  // Fade-In vorbei

    float previous = 0.0f;
    bool havePrevious = false;
    float maxDelta = 0.0f;

    for (int block = 0; block < 450; ++block)
    {
        rig.feedBlocks (1);
        for (int i = 0; i < blockSize; ++i)
        {
            const auto sample = rig.output.getSample (0, i);
            if (havePrevious)
                maxDelta = juce::jmax (maxDelta, std::abs (sample - previous));
            previous = sample;
            havePrevious = true;
        }
    }

    // Kontinuierlicher Playhead: Deltas bleiben bei Signal-Steigung ×
    // (1 + maxSlewRatio) plus Wrap-Blende — ein roher Wall-Clock-Lesekopf
    // läge mit ±48-Sample-Sprüngen um Größenordnungen darüber (> 0.3)
    REQUIRE (maxDelta < 0.02f);
}

TEST_CASE ("LooperEngine: Re-Commit wechselt glitch-frei, Stop blendet aus", "[looper]")
{
    EngineRig rig;
    rig.signal = [] (std::uint64_t) { return 0.4f; };  // konstant → Pegel-Checks

    rig.feedBars (4.5);
    REQUIRE (rig.engine.commit (1, rig.service, 0, 1, rig.anchors).wasOk());
    rig.feedBlocks (4);

    // Konstantes Material: der Loop liefert stabil 0.4
    REQUIRE (std::abs (rig.output.getSample (0, 100) - 0.4f) < 1.0e-3f);

    // Re-Commit auf 2 Takte: während des Voice-Crossfades bleibt die Summe
    // beschränkt (beide Voices tragen dasselbe Material)
    REQUIRE (rig.engine.commit (2, rig.service, 0, 1, rig.anchors).wasOk());
    float maxSum = 0.0f;
    for (int block = 0; block < 4; ++block)
    {
        rig.feedBlocks (1);
        maxSum = juce::jmax (maxSum, rig.output.getMagnitude (0, blockSize));
    }
    REQUIRE (maxSum < 0.81f);  // nie > Summe beider Voices bei Vollpegel
    REQUIRE (rig.engine.getLoopBars() == 2);

    // Nach den Fades: wieder exakt eine Voice
    rig.feedBlocks (4);
    REQUIRE (std::abs (rig.output.getSample (0, 100) - 0.4f) < 1.0e-3f);

    // Stop: 5-ms-Fade, danach Stille und kein isPlaying mehr
    rig.engine.stop();
    rig.feedBlocks (2);
    rig.feedBlocks (1);
    REQUIRE (rig.output.getMagnitude (0, blockSize) < 1.0e-6f);
    REQUIRE_FALSE (rig.engine.isPlaying());
    REQUIRE (rig.engine.getLoopBars() == 0);
}

TEST_CASE ("LooperEngine: Anker-Routing — OOB-Paar schreibt nicht, Rückkehr spielt weiter", "[looper]")
{
    EngineRig rig;
    rig.signal = [] (std::uint64_t) { return 0.4f; };

    rig.feedBars (2.5);
    REQUIRE (rig.engine.commit (1, rig.service, 0, 1, rig.anchors).wasOk());
    rig.feedBlocks (2);  // Fade-In vorbei
    REQUIRE (std::abs (rig.output.getSample (0, 100) - 0.4f) < 1.0e-3f);

    // Anker-Paar 3 = Kanäle 6/7, der Testbuffer hat 2 → kein Write, aber
    // die Voice läuft weiter (Gerätewechsel lässt keine Zombies zurück)
    rig.engine.setAnchor (3);
    rig.feedBlocks (2);
    REQUIRE (rig.output.getMagnitude (0, blockSize) < 1.0e-6f);
    REQUIRE (rig.output.getMagnitude (1, blockSize) < 1.0e-6f);
    REQUIRE (rig.engine.isPlaying());

    // Zurück auf Paar 0: der Loop ist sofort wieder da (Gain stand voll)
    rig.engine.setAnchor (0);
    rig.feedBlocks (1);
    REQUIRE (std::abs (rig.output.getSample (0, 100) - 0.4f) < 1.0e-3f);
    REQUIRE (std::abs (rig.output.getSample (1, 100) - 0.4f) < 1.0e-3f);
}

TEST_CASE ("LooperEngine: Fehlerfälle — Historie, Quelle, Länge", "[looper]")
{
    EngineRig rig;

    // Session-Anfang: noch kein kompletter Takt
    REQUIRE (rig.engine.commit (1, rig.service, 0, 1, rig.anchors).failed());

    rig.feedBars (2.5);

    // Keine Quelle aufgelöst
    REQUIRE (rig.engine.commit (1, rig.service, -1, -1, rig.anchors).failed());

    // Grenzen 1+2 vorhanden (1-Takt-Commit ok), 4 Takte brauchen Grenze 5
    REQUIRE (rig.engine.commit (4, rig.service, 0, 1, rig.anchors).failed());
    REQUIRE (rig.engine.commit (1, rig.service, 0, 1, rig.anchors).wasOk());

    SECTION ("Loop über maxLoopSeconds schlägt fehl (tiefes Tempo)")
    {
        // 30 BPM: 8 Takte = 102.4 s > 60 s (1 Beat = 96 000 Samples)
        rig.engine.stop();
        rig.bpm = 30.0;
        rig.feedBlocks (8 * 4 * 96'000 / blockSize + 200);

        const auto result = rig.engine.commit (8, rig.service, 0, 1, rig.anchors);
        REQUIRE (result.failed());
        REQUIRE (result.getErrorMessage().contains ("60"));
    }
}

TEST_CASE ("LooperEngine: process ist allocation-free (RT-Audit)", "[looper]")
{
    EngineRig rig;
    rig.feedBars (2.5);
    REQUIRE (rig.engine.commit (1, rig.service, 0, 1, rig.anchors).wasOk());

    const auto violationsBefore = conduit::rt::getAllocationViolations();

    {
        const conduit::rt::ScopedRealtimeSection rtAudit;
        rig.feedBlocks (64);
    }

    if (conduit::rt::isHookActive())
        REQUIRE (conduit::rt::getAllocationViolations() == violationsBefore);
}

//==============================================================================
TEST_CASE ("LooperEngine: nebenläufige Commits gegen laufendes Audio (Stress)", "[looper][stress]")
{
    EngineRig rig;
    rig.signal = [] (std::uint64_t) { return 0.4f; };
    rig.feedBars (4.5);

    std::atomic<bool> stop { false };
    std::atomic<int> commits { 0 };

    // Message-Thread-Surrogat: Commits + Stops hämmern
    std::thread committer ([&]
    {
        int bars = 1;
        while (! stop.load (std::memory_order_relaxed))
        {
            if (rig.engine.commit (bars, rig.service, 0, 1, rig.anchors).wasOk())
                commits.fetch_add (1, std::memory_order_relaxed);

            bars = bars == 1 ? 2 : 1;

            if ((commits.load (std::memory_order_relaxed) % 7) == 6)
                rig.engine.stop();

            std::this_thread::yield();
        }
    });

    // Audio-Surrogat: ~4 s Material; Output muss IMMER beschränkt bleiben
    bool bounded = true;
    for (int block = 0; block < 400; ++block)
    {
        rig.feedBlocks (1);
        bounded = bounded && rig.output.getMagnitude (0, blockSize) < 0.81f;
    }

    stop.store (true, std::memory_order_relaxed);
    committer.join();

    REQUIRE (bounded);
    REQUIRE (commits.load() > 0);
}
