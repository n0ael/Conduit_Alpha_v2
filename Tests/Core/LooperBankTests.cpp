#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "Core/Looper/BarSampleAnchors.h"
#include "Core/Looper/LooperBank.h"
#include "Util/RtAllocationGuard.h"

using conduit::BarSampleAnchors;
using conduit::CaptureService;
using conduit::CaptureSettings;
using conduit::LooperBank;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr int    blockSize = 480;
// 120 BPM @ 48 kHz: 1 Beat = 24 000 Samples, 1 Takt = 96 000 Samples

struct TempCaptureSettings
{
    TempCaptureSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitLooperBankTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();

        juce::PropertiesFile::Options options;
        options.applicationName = "ConduitLooperBankTests";
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

/** Audio-Callback-Surrogat (1:1 aus den LooperEngineTests portiert):
    Input-Tap → Takt-Anker → Loop-Playback laufen synchron wie im
    EngineProcessor; das Eingangssignal ist eine Funktion der ABSOLUTEN
    SampleClock-Position (Loop-Inhalt nachrechenbar). */
struct BankRig
{
    BankRig()
    {
        service.prepare (testSampleRate, blockSize, 2);
        bank.prepare (testSampleRate, blockSize);
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
            clock.beatAtBlockStart = beat + beatAxisOffset + nextClockJitterBeats();
            clock.sampleRate = testSampleRate;

            anchors.process (clock, blockStart, blockSize);

            output.clear();
            bank.process (output, 2, clock, blockStart, anchors);

            beat += clock.beatsPerSample() * blockSize;
        }
    }

    void feedBars (double bars) { feedBlocks ((int) std::lround (bars * 4.0 * 24000.0 / blockSize)); }

    /** Commit auf Looper 0 / Track 0 (Paritäts-Semantik der M2-Stufe). */
    [[nodiscard]] juce::Result commit (int bars, int looper = 0, int track = 0)
    {
        return bank.commitAndPlay (looper, track, bars, service, 0, 1, anchors);
    }

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
    LooperBank bank;
    juce::AudioBuffer<float> input  { 2, blockSize };
    juce::AudioBuffer<float> output { 2, blockSize };
    double beat = 0.0;
    double bpm = 120.0;
    double beatAxisOffset = 0.0;   // Link-Grid-Shift-Simulation (Re-Sync)
    double clockJitterBeats = 0.0;
    std::uint32_t jitterState = 0x9e3779b9u;
    std::function<float (std::uint64_t)> signal;
};

/** Blockweises Stetigkeits-Monitoring des Looper-Outputs (Kanal 0). */
struct ContinuityMonitor
{
    void observe (const juce::AudioBuffer<float>& output, int numSamples)
    {
        const auto* data = output.getReadPointer (0);
        for (int i = 0; i < numSamples; ++i)
        {
            if (hasPrevious)
                maxDelta = juce::jmax (maxDelta, std::abs (data[i] - previous));
            previous = data[i];
            hasPrevious = true;
        }
    }

    float maxDelta = 0.0f;
    float previous = 0.0f;
    bool hasPrevious = false;
};

} // namespace

//==============================================================================
TEST_CASE ("LooperBank: Commit ist sample-exakt und phasenstarr zum Session-Beat", "[looper]")
{
    BankRig rig;

    // Sägezahn mit Perioden-Länge 1 Beat, phasenstarr zur SampleClock:
    // an Taktgrenzen (Vielfache von 96 000) startet die Periode bei 0 —
    // der erwartete Loop-Output ist damit 0.5·frac(Phase in Beats)
    rig.signal = [] (std::uint64_t pos)
    { return 0.5f * static_cast<float> (pos % 24000) / 24000.0f; };

    rig.feedBars (2.5);
    REQUIRE (rig.anchors.latestBoundaryBar() >= 2);

    REQUIRE (rig.commit (1).wasOk());
    REQUIRE (rig.bank.isPlaying());
    REQUIRE (rig.bank.getLoopBars() == 1);

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

TEST_CASE ("LooperBank: Wrap-Crossfade hält den Übergang stetig", "[looper]")
{
    BankRig rig;

    // Periode bewusst NICHT bar-synchron: am Wrap springt das Material um
    // ~0.58 — ohne Crossfade wäre der Sprung 1:1 hörbar
    rig.signal = [] (std::uint64_t pos)
    {
        return 0.8f * static_cast<float> (
            std::sin (juce::MathConstants<double>::twoPi
                      * static_cast<double> (pos) / 70000.0));
    };

    rig.feedBars (2.5);
    REQUIRE (rig.commit (1).wasOk());
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

    REQUIRE (maxDelta < 0.05f);
}

TEST_CASE ("LooperBank: Wall-Clock-Jitter erreicht den Lesekopf nicht", "[looper]")
{
    BankRig rig;
    rig.clockJitterBeats = 0.002;

    // 100-Hz-Sinus (Periode 480 Samples): Lesekopf-Sprünge wären als
    // große Sample-Deltas sichtbar (v1-Lektion, LooperEngine-Historie)
    rig.signal = [] (std::uint64_t pos)
    {
        return 0.8f * static_cast<float> (
            std::sin (juce::MathConstants<double>::twoPi
                      * static_cast<double> (pos) / 480.0));
    };

    rig.feedBars (2.5);
    REQUIRE (rig.commit (1).wasOk());
    rig.feedBlocks (2);

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

    REQUIRE (maxDelta < 0.02f);
}

TEST_CASE ("LooperBank: Re-Commit wechselt glitch-frei, Stop blendet aus", "[looper]")
{
    BankRig rig;
    rig.signal = [] (std::uint64_t) { return 0.4f; };  // konstant → Pegel-Checks

    rig.feedBars (4.5);
    REQUIRE (rig.commit (1).wasOk());
    rig.feedBlocks (4);

    REQUIRE (std::abs (rig.output.getSample (0, 100) - 0.4f) < 1.0e-3f);

    // Re-Commit auf 2 Takte: während des Voice-Crossfades bleibt die Summe
    // beschränkt (beide Voices tragen dasselbe Material)
    REQUIRE (rig.commit (2).wasOk());
    float maxSum = 0.0f;
    for (int block = 0; block < 4; ++block)
    {
        rig.feedBlocks (1);
        maxSum = juce::jmax (maxSum, rig.output.getMagnitude (0, blockSize));
    }
    REQUIRE (maxSum < 0.81f);
    REQUIRE (rig.bank.getLoopBars() == 2);

    rig.feedBlocks (4);
    REQUIRE (std::abs (rig.output.getSample (0, 100) - 0.4f) < 1.0e-3f);

    // Stop: 5-ms-Fade, danach Stille und kein isPlaying mehr
    rig.bank.stopAll();
    rig.feedBlocks (3);
    REQUIRE (rig.output.getMagnitude (0, blockSize) < 1.0e-6f);
    REQUIRE_FALSE (rig.bank.isPlaying());
    REQUIRE (rig.bank.getLoopBars() == 0);
}

TEST_CASE ("LooperBank: Re-Commit gibt den alten Clip frei (Retire-Protokoll)", "[looper]")
{
    BankRig rig;
    rig.feedBars (4.5);

    REQUIRE (rig.commit (1).wasOk());
    const auto oneClip = rig.bank.getRamBytesUsed();
    REQUIRE (oneClip > 0);

    REQUIRE (rig.commit (1).wasOk());
    REQUIRE (rig.bank.getRamBytesUsed() >= 2 * oneClip);

    // Audio verarbeitet deleteClip + Voice-Fade, quittiert den alten Clip;
    // serviceMessageThread gibt ihn frei — RAM fällt auf einen Clip zurück
    rig.feedBlocks (4);
    rig.bank.serviceMessageThread();
    REQUIRE (rig.bank.getRamBytesUsed() == oneClip);
    REQUIRE (rig.bank.isPlaying());
}

TEST_CASE ("LooperBank: RAM-Budget begrenzt Commits sauber", "[looper]")
{
    BankRig rig;
    rig.feedBars (2.5);

    rig.bank.setRamBudgetBytes (1000);  // unter jeder Clip-Größe

    const auto result = rig.commit (1);
    REQUIRE (result.failed());
    REQUIRE (result.getErrorMessage().contains ("RAM"));
    REQUIRE_FALSE (rig.bank.isPlaying());

    // Budget zurück → derselbe Commit geht durch
    rig.bank.setRamBudgetBytes (LooperBank::defaultRamBudgetBytes);
    REQUIRE (rig.commit (1).wasOk());
}

TEST_CASE ("LooperBank: Anker-Routing — OOB-Paar schreibt nicht, Rückkehr spielt weiter", "[looper]")
{
    BankRig rig;
    rig.signal = [] (std::uint64_t) { return 0.4f; };

    rig.feedBars (2.5);
    REQUIRE (rig.commit (1).wasOk());
    rig.feedBlocks (2);
    REQUIRE (std::abs (rig.output.getSample (0, 100) - 0.4f) < 1.0e-3f);

    // Anker-Paar 3 = Kanäle 6/7, der Testbuffer hat 2 → kein Write, aber
    // die Voice läuft weiter (Gerätewechsel lässt keine Zombies zurück)
    rig.bank.setAnchor (3);
    rig.feedBlocks (2);
    REQUIRE (rig.output.getMagnitude (0, blockSize) < 1.0e-6f);
    REQUIRE (rig.output.getMagnitude (1, blockSize) < 1.0e-6f);
    REQUIRE (rig.bank.isPlaying());

    rig.bank.setAnchor (0);
    rig.feedBlocks (1);
    REQUIRE (std::abs (rig.output.getSample (0, 100) - 0.4f) < 1.0e-3f);
    REQUIRE (std::abs (rig.output.getSample (1, 100) - 0.4f) < 1.0e-3f);
}

TEST_CASE ("LooperBank: Beat-Achsen-Sprung (Link-Re-Sync) re-synct klickfrei", "[looper]")
{
    BankRig rig;

    // 100-Hz-Sinus: 96 000 Samples/Takt = exakt 200 Perioden → der Loop
    // wrappt nahtlos, jede Diskontinuität stammt vom Playback selbst
    rig.signal = [] (std::uint64_t pos)
    {
        return 0.5f * static_cast<float> (
            std::sin (juce::MathConstants<double>::twoPi
                      * static_cast<double> (pos) / 480.0));
    };

    rig.feedBars (2.5);
    REQUIRE (rig.commit (1).wasOk());
    rig.feedBlocks (2);

    ContinuityMonitor monitor;
    rig.beatAxisOffset = 0.3;

    for (int block = 0; block < 3 * 200; ++block)
    {
        rig.feedBlocks (1);
        monitor.observe (rig.output, blockSize);
    }

    REQUIRE (monitor.maxDelta < 0.02f);
    REQUIRE (rig.bank.getSnapCount() == 1);
    REQUIRE (rig.bank.isPlaying());
    REQUIRE (rig.output.getMagnitude (0, blockSize) > 0.1f);
}

TEST_CASE ("LooperBank: kurzer Wall-Clock-Spike wird OHNE Re-Sync absorbiert", "[looper]")
{
    BankRig rig;
    rig.signal = [] (std::uint64_t pos)
    {
        return 0.5f * static_cast<float> (
            std::sin (juce::MathConstants<double>::twoPi
                      * static_cast<double> (pos) / 480.0));
    };

    rig.feedBars (2.5);
    REQUIRE (rig.commit (1).wasOk());
    rig.feedBlocks (2);

    const auto beatsPerBlock = (120.0 / (60.0 * testSampleRate)) * blockSize;
    while (std::floor ((rig.beat + 2.0 * beatsPerBlock) / 4.0)
               <= std::floor (rig.beat / 4.0))
        rig.feedBlocks (1);

    ContinuityMonitor monitor;
    rig.beatAxisOffset = 0.25;
    for (int block = 0; block < 2; ++block)   // Grenze ankert im Spike
    {
        rig.feedBlocks (1);
        monitor.observe (rig.output, blockSize);
    }
    rig.beatAxisOffset = 0.0;

    for (int block = 0; block < 3 * 200; ++block)
    {
        rig.feedBlocks (1);
        monitor.observe (rig.output, blockSize);
    }

    REQUIRE (monitor.maxDelta < 0.02f);
    REQUIRE (rig.bank.getSnapCount() == 0);   // absorbiert, nicht gesnappt
    REQUIRE (rig.bank.isPlaying());
}

TEST_CASE ("LooperBank: Fehlerfälle — Historie, Quelle, Länge, Indizes", "[looper]")
{
    BankRig rig;

    // Session-Anfang: noch kein kompletter Takt
    REQUIRE (rig.commit (1).failed());

    rig.feedBars (2.5);

    // Keine Quelle aufgelöst
    REQUIRE (rig.bank.commitAndPlay (0, 0, 1, rig.service, -1, -1, rig.anchors).failed());

    // Ungültige Looper-/Track-Indizes
    REQUIRE (rig.commit (1, -1, 0).failed());
    REQUIRE (rig.commit (1, LooperBank::maxLoopers, 0).failed());
    REQUIRE (rig.commit (1, 0, LooperBank::maxTracks).failed());

    // Grenzen 1+2 vorhanden (1-Takt-Commit ok), 4 Takte brauchen Grenze 5
    REQUIRE (rig.commit (4).failed());
    REQUIRE (rig.commit (1).wasOk());

    SECTION ("Loop über maxLoopSeconds schlägt fehl (tiefes Tempo)")
    {
        rig.bank.stopAll();
        rig.bpm = 30.0;
        rig.feedBlocks (8 * 4 * 96'000 / blockSize + 200);

        const auto result = rig.commit (8);
        REQUIRE (result.failed());
        REQUIRE (result.getErrorMessage().contains ("60"));
    }
}

TEST_CASE ("LooperBank: process ist allocation-free (RT-Audit)", "[looper]")
{
    BankRig rig;
    rig.feedBars (2.5);
    REQUIRE (rig.commit (1).wasOk());

    const auto violationsBefore = conduit::rt::getAllocationViolations();

    {
        const conduit::rt::ScopedRealtimeSection rtAudit;
        rig.feedBlocks (64);
    }

    if (conduit::rt::isHookActive())
        REQUIRE (conduit::rt::getAllocationViolations() == violationsBefore);
}

//==============================================================================
// M3 — Quantisierung, Rate/Reverse/÷2, Track-Mix

TEST_CASE ("LooperBank: quantisierter Start landet sample-genau auf der Taktgrenze", "[looper]")
{
    BankRig rig;
    rig.signal = [] (std::uint64_t) { return 0.4f; };

    rig.feedBars (2.5);
    REQUIRE (rig.commit (1).wasOk());
    rig.feedBlocks (4);

    // Stoppen, Stille abwarten
    rig.bank.stopTrack (0, 0, 0.0);
    rig.feedBlocks (3);
    REQUIRE (rig.output.getMagnitude (0, blockSize) < 1.0e-6f);

    // Quantisierter Start (1 Takt): bis zur nächsten Grenze bleibt es still
    REQUIRE (rig.bank.startTrack (0, 0, 4.0).wasOk());

    const auto beatsPerSample = 120.0 / (60.0 * testSampleRate);
    double foundBeat = -1.0;

    for (int block = 0; block < 300 && foundBeat < 0.0; ++block)
    {
        const auto blockStartBeat = rig.beat;
        rig.feedBlocks (1);

        for (int i = 0; i < blockSize; ++i)
            if (std::abs (rig.output.getSample (0, i)) > 1.0e-6f)
            {
                foundBeat = blockStartBeat + beatsPerSample * i;
                break;
            }
    }

    REQUIRE (foundBeat >= 0.0);

    // Erster hörbarer Sample liegt auf der Taktgrenze (± 2 Samples)
    const auto beatInBar = foundBeat - std::floor (foundBeat / 4.0) * 4.0;
    const auto tolerance = 2.0 * beatsPerSample;
    REQUIRE ((beatInBar < tolerance || beatInBar > 4.0 - tolerance));
    REQUIRE (rig.bank.isTrackPlaying (0, 0));
}

TEST_CASE ("LooperBank: quantisierter Stop endet an der Taktgrenze (+ Fade)", "[looper]")
{
    BankRig rig;
    rig.signal = [] (std::uint64_t) { return 0.4f; };

    rig.feedBars (2.5);
    REQUIRE (rig.commit (1).wasOk());
    rig.feedBlocks (4);

    rig.bank.stopTrack (0, 0, 4.0);
    REQUIRE_FALSE (rig.bank.isTrackPlaying (0, 0));

    const auto beatsPerSample = 120.0 / (60.0 * testSampleRate);
    double lastAudibleBeat = -1.0;

    for (int block = 0; block < 300; ++block)
    {
        const auto blockStartBeat = rig.beat;
        rig.feedBlocks (1);

        bool silent = true;
        for (int i = 0; i < blockSize; ++i)
            if (std::abs (rig.output.getSample (0, i)) > 1.0e-6f)
            {
                lastAudibleBeat = blockStartBeat + beatsPerSample * i;
                silent = false;
            }

        if (silent && lastAudibleBeat >= 0.0)
            break;
    }

    REQUIRE (lastAudibleBeat >= 0.0);

    // Letzter hörbarer Sample: Taktgrenze + 5-ms-Fade (240 Samples)
    const auto beatInBar = lastAudibleBeat - std::floor (lastAudibleBeat / 4.0) * 4.0;
    REQUIRE (beatInBar < (240.0 + 4.0) * beatsPerSample);
}

TEST_CASE ("LooperBank: Retrigger setzt die Phase auf den Ausführungspunkt", "[looper]")
{
    BankRig rig;

    // Sägezahn mit 1-Beat-Periode: Content-Beat x ↦ 0.5·frac(x)
    rig.signal = [] (std::uint64_t pos)
    { return 0.5f * static_cast<float> (pos % 24000) / 24000.0f; };

    rig.feedBars (2.5);
    REQUIRE (rig.commit (1).wasOk());
    rig.feedBlocks (4);

    // Retrigger sofort: Anker = Blockanfangs-Beat des Ausführungsblocks
    REQUIRE (rig.bank.retriggerTrack (0, 0, 0.0).wasOk());
    const auto retriggerBeat = rig.beat;   // Kommando greift im nächsten Block
    rig.feedBlocks (2);                    // Fade-In vorbei

    int checked = 0;
    for (int block = 0; block < 40; ++block)
    {
        const auto blockStartBeat = rig.beat;
        rig.feedBlocks (1);

        for (int i = 0; i < blockSize; i += 11)
        {
            const auto beatAt = blockStartBeat + (120.0 / (60.0 * testSampleRate)) * i;
            const auto phase = beatAt - retriggerBeat
                             - std::floor ((beatAt - retriggerBeat) / 4.0) * 4.0;

            const auto fracPhase = phase - std::floor (phase);
            if (fracPhase < 0.05 || fracPhase > 0.95)
                continue;  // Sägezahn-Sprung
            if (phase > 3.98)
                continue;  // Wrap-Zone

            const auto expected = 0.5f * static_cast<float> (fracPhase);
            REQUIRE (std::abs (rig.output.getSample (0, i) - expected) < 6.0e-3f);
            ++checked;
        }
    }

    REQUIRE (checked > 1'000);
}

TEST_CASE ("LooperBank: Reset mit Sync stellt die bar-gelockte Phase wieder her", "[looper]")
{
    BankRig rig;
    rig.signal = [] (std::uint64_t pos)
    { return 0.5f * static_cast<float> (pos % 24000) / 24000.0f; };

    rig.feedBars (2.5);
    REQUIRE (rig.commit (1).wasOk());
    rig.feedBlocks (4);

    // Phase per Retrigger mitten im Takt verstellen
    while (rig.beat - std::floor (rig.beat / 4.0) * 4.0 < 1.3
           || rig.beat - std::floor (rig.beat / 4.0) * 4.0 > 1.8)
        rig.feedBlocks (1);
    REQUIRE (rig.bank.retriggerTrack (0, 0, 0.0).wasOk());
    rig.feedBlocks (4);

    // Reset mit Sync → nach Splice-Duck (~2 Blöcke) wieder 0.5·frac(beat)
    REQUIRE (rig.bank.resetClipWithSync (0, 0).wasOk());
    rig.feedBlocks (6);

    int checked = 0;
    for (int block = 0; block < 30; ++block)
    {
        const auto blockStartBeat = rig.beat;
        rig.feedBlocks (1);

        for (int i = 0; i < blockSize; i += 13)
        {
            const auto beatAt = blockStartBeat + (120.0 / (60.0 * testSampleRate)) * i;
            const auto fracBeat = beatAt - std::floor (beatAt);
            const auto beatInBar = beatAt - std::floor (beatAt / 4.0) * 4.0;
            if (fracBeat < 0.05 || fracBeat > 0.95 || beatInBar > 3.98)
                continue;

            const auto expected = 0.5f * static_cast<float> (fracBeat);
            REQUIRE (std::abs (rig.output.getSample (0, i) - expected) < 6.0e-3f);
            ++checked;
        }
    }

    REQUIRE (checked > 700);
}

TEST_CASE ("LooperBank: VARI-Rate — klickfreier Wechsel, halbe Rate = halbe Frequenz", "[looper]")
{
    BankRig rig;

    // 100-Hz-Sinus (Periode 480 Samples), bar-synchron (200 Perioden/Takt)
    rig.signal = [] (std::uint64_t pos)
    {
        return 0.5f * static_cast<float> (
            std::sin (juce::MathConstants<double>::twoPi
                      * static_cast<double> (pos) / 480.0));
    };

    rig.feedBars (2.5);
    REQUIRE (rig.commit (1).wasOk());
    rig.feedBlocks (2);

    const auto countZeroCrossings = [&] (int blocks)
    {
        int crossings = 0;
        float previous = rig.output.getSample (0, blockSize - 1);
        for (int block = 0; block < blocks; ++block)
        {
            rig.feedBlocks (1);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto sample = rig.output.getSample (0, i);
                if ((previous < 0.0f && sample >= 0.0f) || (previous > 0.0f && sample <= 0.0f))
                    ++crossings;
                previous = sample;
            }
        }
        return crossings;
    };

    const auto fullRate = countZeroCrossings (200);   // 1 Takt

    // Rate 0.5×: positions-kontinuierlich (kein Klick) und halbe Frequenz
    ContinuityMonitor monitor;
    REQUIRE (rig.bank.setClipRate (0, 0, 0.5).wasOk());
    for (int block = 0; block < 4; ++block)
    {
        rig.feedBlocks (1);
        monitor.observe (rig.output, blockSize);
    }
    REQUIRE (monitor.maxDelta < 0.02f);

    const auto halfRate = countZeroCrossings (200);
    REQUIRE (halfRate > fullRate * 4 / 10);
    REQUIRE (halfRate < fullRate * 6 / 10);
}

TEST_CASE ("LooperBank: Reverse — sofort dreht die Richtung, Boundary-Modus wartet", "[looper]")
{
    BankRig rig;

    // Sägezahn: vorwärts = viele kleine POSITIVE Deltas, rückwärts = negative
    rig.signal = [] (std::uint64_t pos)
    { return 0.5f * static_cast<float> (pos % 24000) / 24000.0f; };

    rig.feedBars (2.5);
    REQUIRE (rig.commit (1).wasOk());
    rig.feedBlocks (4);

    const auto rampDeltaBalance = [&] (int blocks)
    {
        int positive = 0, negative = 0;
        float previous = 0.0f;
        bool hasPrevious = false;
        for (int block = 0; block < blocks; ++block)
        {
            rig.feedBlocks (1);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto sample = rig.output.getSample (0, i);
                if (hasPrevious)
                {
                    const auto delta = sample - previous;
                    if (std::abs (delta) < 0.1f)
                    {
                        if (delta > 1.0e-7f)  ++positive;
                        if (delta < -1.0e-7f) ++negative;
                    }
                }
                previous = sample;
                hasPrevious = true;
            }
        }
        return positive - negative;
    };

    REQUIRE (rampDeltaBalance (50) > 10'000);   // vorwärts

    SECTION ("sofort: Richtung dreht im nächsten Block")
    {
        REQUIRE (rig.bank.toggleClipReverse (0, 0, false).wasOk());
        rig.feedBlocks (2);
        REQUIRE (rampDeltaBalance (50) < -10'000);

        // Zurück: wieder vorwärts
        REQUIRE (rig.bank.toggleClipReverse (0, 0, false).wasOk());
        rig.feedBlocks (2);
        REQUIRE (rampDeltaBalance (50) > 10'000);
    }

    SECTION ("an der Loop-Grenze: bis zum Wrap bleibt es vorwärts")
    {
        // Kurz nach einer Taktgrenze togglen → fast ein ganzer Takt vorwärts
        while (rig.beat - std::floor (rig.beat / 4.0) * 4.0 > 0.4)
            rig.feedBlocks (1);
        REQUIRE (rig.bank.toggleClipReverse (0, 0, true).wasOk());

        // Die nächsten ~2 Beats: noch vorwärts
        REQUIRE (rampDeltaBalance (100) > 5'000);

        // Nach dem Wrap (restlicher Takt + Reserve): rückwärts
        rig.feedBars (1.0);
        REQUIRE (rampDeltaBalance (50) < -10'000);
    }
}

TEST_CASE ("LooperBank: ×2/÷2 ändern nur L — Fenster-Wahl erste/aktuelle Hälfte", "[looper]")
{
    BankRig rig;

    // Sägezahn mit 2-TAKT-Periode: Content-Beat x ∈ [0, 8) ↦ 0.5·x/8 —
    // erste und zweite Content-Hälfte sind damit unterscheidbar
    rig.signal = [] (std::uint64_t pos)
    { return 0.5f * static_cast<float> (pos % 192000) / 192000.0f; };

    rig.feedBars (4.5);
    REQUIRE (rig.commit (2).wasOk());
    rig.feedBlocks (4);

    const auto sampleMidBar = [&]() -> float
    {
        // Mitte des nächsten Takts abgreifen (weit weg von Wrap + Periodensprung)
        while (rig.beat - std::floor (rig.beat / 4.0) * 4.0 > 0.5)
            rig.feedBlocks (1);
        while (rig.beat - std::floor (rig.beat / 4.0) * 4.0 < 1.9)
            rig.feedBlocks (1);
        return rig.output.getSample (0, blockSize / 2);
    };

    SECTION ("÷2 erste Hälfte: nur Werte der ersten Content-Hälfte")
    {
        REQUIRE (rig.bank.multiplyClipLength (0, 0, false,
                                              conduit::looper::HalveMode::firstHalf).wasOk());
        rig.feedBlocks (6);  // ggf. Splice-Duck abwarten

        for (int bar = 0; bar < 4; ++bar)
        {
            const auto value = sampleMidBar();
            // Erste Hälfte des Contents: Beats [0, 4) → Werte 0 … 0.25
            REQUIRE (value < 0.26f);
        }
    }

    SECTION ("÷2 aktuelle Hälfte aus der zweiten: Werte der zweiten Content-Hälfte")
    {
        // In die zweite Content-Hälfte laufen (Phase ∈ [4, 8))
        while (rig.beat - std::floor (rig.beat / 8.0) * 8.0 < 5.0)
            rig.feedBlocks (1);

        REQUIRE (rig.bank.multiplyClipLength (0, 0, false,
                                              conduit::looper::HalveMode::currentHalf).wasOk());
        rig.feedBlocks (6);

        for (int bar = 0; bar < 4; ++bar)
        {
            const auto value = sampleMidBar();
            // Zweite Hälfte: Beats [4, 8) → Werte 0.25 … 0.5
            REQUIRE (value > 0.24f);
        }
    }

    SECTION ("×2 nach ÷2 stellt den vollen Loop wieder her")
    {
        REQUIRE (rig.bank.multiplyClipLength (0, 0, false,
                                              conduit::looper::HalveMode::firstHalf).wasOk());
        rig.feedBlocks (6);
        REQUIRE (rig.bank.multiplyClipLength (0, 0, true,
                                              conduit::looper::HalveMode::firstHalf).wasOk());
        rig.feedBlocks (6);

        // Voller 2-Takt-Loop: Werte erreichen wieder > 0.26 (zweite Hälfte)
        float maxValue = 0.0f;
        for (int bar = 0; bar < 4; ++bar)
            maxValue = juce::jmax (maxValue, sampleMidBar());
        REQUIRE (maxValue > 0.26f);

        // ×2 über den Content hinaus clampt (Info bleibt 8 Beats)
        REQUIRE (rig.bank.multiplyClipLength (0, 0, true,
                                              conduit::looper::HalveMode::firstHalf).wasOk());
        LooperBank::ClipInfo info;
        REQUIRE (rig.bank.getClipInfo (0, 0, info));
        REQUIRE (info.lengthBeats == 8.0);
    }
}

TEST_CASE ("LooperBank: Track-Mix — Gain, Pan (Balance), Mute, Solo-Scope", "[looper]")
{
    BankRig rig;
    rig.signal = [] (std::uint64_t) { return 0.4f; };

    rig.feedBars (2.5);
    REQUIRE (rig.commit (1, 0, 0).wasOk());
    rig.feedBlocks (4);

    SECTION ("Gain 0.5 halbiert den Pegel (nach 5-ms-Slew)")
    {
        rig.bank.setTrackGain (0, 0, 0.5f);
        rig.feedBlocks (3);
        REQUIRE (std::abs (rig.output.getSample (0, 100) - 0.2f) < 5.0e-3f);
    }

    SECTION ("Pan −1: links Unity, rechts still (Balance-Gesetz)")
    {
        rig.bank.setTrackPan (0, 0, -1.0f);
        rig.feedBlocks (3);
        REQUIRE (std::abs (rig.output.getSample (0, 100) - 0.4f) < 5.0e-3f);
        REQUIRE (std::abs (rig.output.getSample (1, 100)) < 5.0e-3f);
    }

    SECTION ("Mute → Stille, Unmute → zurück")
    {
        rig.bank.setTrackMute (0, 0, true);
        rig.feedBlocks (3);
        REQUIRE (rig.output.getMagnitude (0, blockSize) < 1.0e-3f);

        rig.bank.setTrackMute (0, 0, false);
        rig.feedBlocks (3);
        REQUIRE (std::abs (rig.output.getSample (0, 100) - 0.4f) < 5.0e-3f);
    }

    SECTION ("Solo-Scope: pro Looper vs. global")
    {
        // Drei Tracks: Looper 0/Track 0+1, Looper 1/Track 0 — Summe 1.2
        REQUIRE (rig.commit (1, 0, 1).wasOk());
        REQUIRE (rig.commit (1, 1, 0).wasOk());
        rig.feedBlocks (4);
        REQUIRE (std::abs (rig.output.getSample (0, 100) - 1.2f) < 1.0e-2f);

        // Solo (0,1), Scope pro Looper: (0,0) stumm, Looper 1 läuft weiter
        rig.bank.setTrackSolo (0, 1, true);
        rig.feedBlocks (3);
        REQUIRE (std::abs (rig.output.getSample (0, 100) - 0.8f) < 1.0e-2f);

        // Scope global: auch Looper 1 stumm → nur der Solo-Track bleibt
        rig.bank.setSoloScopeGlobal (true);
        rig.feedBlocks (3);
        REQUIRE (std::abs (rig.output.getSample (0, 100) - 0.4f) < 1.0e-2f);

        // Solo weg → alles zurück
        rig.bank.setTrackSolo (0, 1, false);
        rig.feedBlocks (3);
        REQUIRE (std::abs (rig.output.getSample (0, 100) - 1.2f) < 1.0e-2f);
    }

    SECTION ("Post-Fader-Meter folgt dem Fader")
    {
        rig.feedBlocks (20);   // RMS-Fenster (150 ms) füllen
        REQUIRE (rig.bank.getTrackMeter (0, 0).getPeak (0) > 0.35f);
        REQUIRE (rig.bank.getTrackMeter (0, 0).getRms (0) > 0.3f);

        // Fader zu → RMS fällt (Peak hat 1,5-s-Ballistik, dauert länger)
        rig.bank.setTrackGain (0, 0, 0.0f);
        rig.feedBlocks (100);   // RMS fällt mit exp(−t/0.3 s) → ~0.014 nach 1 s
        REQUIRE (rig.bank.getTrackMeter (0, 0).getRms (0) < 0.05f);
    }
}

TEST_CASE ("LooperBank: 16 Tracks parallel — beschränkt und allocation-free", "[looper]")
{
    BankRig rig;
    rig.signal = [] (std::uint64_t) { return 0.4f; };

    rig.feedBars (2.5);
    for (int l = 0; l < LooperBank::maxLoopers; ++l)
        for (int t = 0; t < LooperBank::maxTracks; ++t)
            REQUIRE (rig.commit (1, l, t).wasOk());

    rig.feedBlocks (4);
    REQUIRE (std::abs (rig.output.getSample (0, 100) - 16.0f * 0.4f) < 0.05f);

    const auto violationsBefore = conduit::rt::getAllocationViolations();
    {
        const conduit::rt::ScopedRealtimeSection rtAudit;
        rig.feedBlocks (64);
    }
    if (conduit::rt::isHookActive())
        REQUIRE (conduit::rt::getAllocationViolations() == violationsBefore);

    // Aktionen auf allen Tracks bleiben beschränkt
    for (int l = 0; l < LooperBank::maxLoopers; ++l)
        rig.bank.stopTrack (l, 2, 0.0);
    rig.feedBlocks (3);
    REQUIRE (std::abs (rig.output.getSample (0, 100) - 12.0f * 0.4f) < 0.05f);
}

TEST_CASE ("LooperBank: Aktionen ohne Clip schlagen sauber fehl", "[looper]")
{
    BankRig rig;
    rig.feedBars (1.5);

    REQUIRE (rig.bank.startTrack (0, 0, 0.0).failed());
    REQUIRE (rig.bank.retriggerTrack (0, 0, 0.0).failed());
    REQUIRE (rig.bank.setClipRate (0, 0, 2.0).failed());
    REQUIRE (rig.bank.toggleClipReverse (0, 0, false).failed());
    REQUIRE (rig.bank.resetClipWithSync (0, 0).failed());

    LooperBank::ClipInfo info;
    REQUIRE_FALSE (rig.bank.getClipInfo (0, 0, info));
}

//==============================================================================
TEST_CASE ("LooperBank: nebenläufige Commits gegen laufendes Audio (Stress)", "[looper][stress]")
{
    BankRig rig;
    rig.signal = [] (std::uint64_t) { return 0.4f; };
    rig.feedBars (4.5);

    std::atomic<bool> stop { false };
    std::atomic<int> commits { 0 };

    // Message-Thread-Surrogat: Commits + Stops hämmern (commitAndPlay ruft
    // serviceMessageThread intern — Retire-Konsument bleibt dieser Thread)
    std::thread committer ([&]
    {
        int bars = 1;
        while (! stop.load (std::memory_order_relaxed))
        {
            if (rig.commit (bars).wasOk())
                commits.fetch_add (1, std::memory_order_relaxed);

            bars = bars == 1 ? 2 : 1;

            if ((commits.load (std::memory_order_relaxed) % 7) == 6)
                rig.bank.stopAll();

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

    // Aufräumen konvergiert: alles quittiert und freigegeben bis auf den
    // zuletzt aktiven Clip (Store) — kein Leak im Graveyard. Service pro
    // Block interleaved, damit die Retire-Queue den Drain nie blockiert.
    rig.bank.stopAll();
    for (int block = 0; block < 8; ++block)
    {
        rig.feedBlocks (1);
        rig.bank.serviceMessageThread();
    }
    REQUIRE (rig.bank.getRamBytesUsed() < 3 * 2 * 4 * (2 * 96'000 + 240 + 4096));
}
