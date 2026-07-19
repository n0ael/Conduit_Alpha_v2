#include <cmath>
#include <functional>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "Core/Looper/BarSampleAnchors.h"
#include "Core/Looper/LooperBank.h"
#include "Core/Looper/LooperClipExporter.h"

using conduit::BarSampleAnchors;
using conduit::CaptureService;
using conduit::CaptureSettings;
using conduit::CaptureWriter;
using conduit::LooperBank;
using conduit::LooperClip;
using conduit::LooperClipExporter;

namespace
{

constexpr double testSampleRate = 48000.0;

/** Clip mit bekanntem Content (Rampe pro Kanal) direkt bauen — der
    Exporter braucht keinen Bank-Commit, nur Buffer + Konstanten. */
std::unique_ptr<LooperClip> makeTestClip (int contentSamples, int leadIn,
                                          std::uint64_t commitStart,
                                          int numChannels = 2)
{
    auto clip = std::make_unique<LooperClip>();
    clip->buffer.setSize (numChannels, contentSamples + leadIn);
    clip->buffer.clear();
    clip->numContentSamples = contentSamples;
    clip->crossfadeSamples = leadIn;
    clip->commitStartSample = commitStart;
    clip->clipId = 7;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* data = clip->buffer.getWritePointer (channel);
        for (int i = 0; i < contentSamples; ++i)
            data[leadIn + i] = (channel == 0 ? 1.0f : -1.0f)
                             * static_cast<float> (i) / static_cast<float> (contentSamples);
    }

    return clip;
}

} // namespace

//==============================================================================
TEST_CASE ("LooperClipExporter: makeJob — Tasks, Pins, sample-exakte Reads", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    const auto clip = makeTestClip (4096, 240, 96'000);
    auto job = LooperClipExporter::makeJob (*clip, "looper1_clip7", testSampleRate);

    // Pin gesetzt (Delete/prepare parken den Clip im Graveyard)
    REQUIRE (clip->exportPins.load() == 1);

    REQUIRE (job.tasks.size() == 2);
    REQUIRE (job.tasks[0].trackName == "looper1_clip7_l");
    REQUIRE (job.tasks[1].trackName == "looper1_clip7_r");
    REQUIRE (job.tasks[0].startPosition == 96'000);
    REQUIRE (job.tasks[0].endPosition == 96'000 + 4096);
    REQUIRE (job.tasks[0].source.ringCapacitySamples == 0);   // eingefroren

    // Reads liefern exakt die Content-Region (hinter dem Lead-in)
    std::vector<float> chunk (512, 0.0f);
    REQUIRE (job.tasks[0].source.read (96'000 + 100, chunk.data(), 512));
    for (int i = 0; i < 512; ++i)
        REQUIRE (juce::exactlyEqual (chunk[(size_t) i],
                                     static_cast<float> (100 + i) / 4096.0f));

    REQUIRE (job.tasks[1].source.read (96'000, chunk.data(), 16));
    REQUIRE (chunk[1] < 0.0f);   // rechter Kanal ist negiert

    // Außerhalb des Contents: sauberes false (kein Ring-Fallback)
    REQUIRE_FALSE (job.tasks[0].source.read (96'000 + 4096 - 8, chunk.data(), 16));
    REQUIRE_FALSE (job.tasks[0].source.read (95'000, chunk.data(), 16));

    // releaseResources löst den Pin (läuft sonst auf dem Writer-Thread)
    job.releaseResources();
    REQUIRE (clip->exportPins.load() == 0);
}

TEST_CASE ("LooperClipExporter: Mono-Clip exportiert EINE Datei ohne Suffix", "[looper]")
{
    // Looper-I/O 07/2026: Mono-Quellen erzeugen 1-Kanal-Clips — die
    // Save-Geste schreibt dann eine echte Mono-Datei (kein _l/_r-Paar)
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    const auto clip = makeTestClip (2048, 240, 96'000, 1);
    auto job = LooperClipExporter::makeJob (*clip, "looper1_mono", testSampleRate);

    REQUIRE (job.tasks.size() == 1);
    REQUIRE (job.tasks[0].trackName == "looper1_mono");
    REQUIRE (job.tasks[0].startPosition == 96'000);
    REQUIRE (job.tasks[0].endPosition == 96'000 + 2048);

    std::vector<float> chunk (64, 0.0f);
    REQUIRE (job.tasks[0].source.read (96'000 + 100, chunk.data(), 64));
    REQUIRE (juce::exactlyEqual (chunk[0], 100.0f / 2048.0f));

    job.releaseResources();
    REQUIRE (clip->exportPins.load() == 0);
}

TEST_CASE ("LooperClipExporter: End-to-End — BWF-Datei ist sample-exakt", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("ConduitLooperExportTests")
                               .getChildFile (juce::Uuid().toString());
    directory.createDirectory();

    const auto clip = makeTestClip (8192, 240, 48'000);

    CaptureWriter writer;
    CaptureWriter::Report report;
    juce::WaitableEvent done;
    writer.onJobFinished = [&] (const CaptureWriter::Report& finished)
    {
        report = finished;
        done.signal();
    };

    auto job = LooperClipExporter::makeJob (*clip, "clip_export", testSampleRate);
    job.bitDepth = 24;
    job.directory = directory;
    job.takeNumber = 1;
    writer.enqueueJob (std::move (job));

    REQUIRE (done.wait (10'000));
    REQUIRE (report.numSucceeded == 2);
    REQUIRE (report.numFailed == 0);
    REQUIRE (clip->exportPins.load() == 0);   // releaseResources lief

    // Datei zurücklesen und gegen den Clip-Content vergleichen
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader (
        format.createReaderFor (report.tasks[0].file.createInputStream().release(),
                                true));
    REQUIRE (reader != nullptr);
    REQUIRE (static_cast<int> (reader->lengthInSamples) == 8192);

    juce::AudioBuffer<float> read (1, 8192);
    REQUIRE (reader->read (&read, 0, 8192, 0, true, false));

    const auto* content = clip->buffer.getReadPointer (0);
    for (int i = 0; i < 8192; i += 37)
        REQUIRE (std::abs (read.getSample (0, i) - content[240 + i]) < 2.0e-4f);

    reader.reset();
    directory.deleteRecursively();
}

TEST_CASE ("LooperClipExporter: Pin hält den Clip über ein Delete am Leben", "[looper]")
{
    // Bank-Rig (Muster LooperBankTests): commit → Export-Pin → Delete →
    // der Graveyard gibt erst nach dem Unpin frei
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ConduitLooperExportPinTests")
                            .getChildFile (juce::Uuid().toString());
    folder.createDirectory();

    juce::PropertiesFile::Options options;
    options.applicationName = "ConduitLooperExportPinTests";
    options.filenameSuffix  = ".settings";
    options.folderName      = folder.getFullPathName();

    {
        CaptureSettings settings { options };
        CaptureService service { settings };
        BarSampleAnchors anchors;
        LooperBank bank;

        constexpr int blockSize = 480;
        service.prepare (testSampleRate, blockSize, 2);
        bank.prepare (testSampleRate, blockSize);
        service.setChannelArmed (0, true);
        service.setChannelArmed (1, true);

        juce::AudioBuffer<float> input { 2, blockSize };
        juce::AudioBuffer<float> output { 2, blockSize };
        double beat = 0.0;

        const auto feed = [&] (int blocks)
        {
            for (int b = 0; b < blocks; ++b)
            {
                const auto blockStart = service.getSampleClock().now();
                for (int ch = 0; ch < 2; ++ch)
                    juce::FloatVectorOperations::fill (input.getWritePointer (ch),
                                                       0.4f, blockSize);
                service.processInputTap (input, 2);

                conduit::ClockState clock;
                clock.bpm = 120.0;
                clock.beatAtBlockStart = beat;
                clock.sampleRate = testSampleRate;
                anchors.process (clock, blockStart, blockSize);

                output.clear();
                bank.process (output, 2, clock, blockStart, anchors);
                beat += clock.beatsPerSample() * blockSize;
            }
        };

        feed (3);
        service.runRamGuard();
        feed (500);   // 2,5 Takte

        LooperClip* clip = nullptr;
        REQUIRE (bank.commitClip (0, 0, 1, service, 0, 1, anchors, &clip).wasOk());
        REQUIRE (clip != nullptr);

        // Export-Pin simulieren, dann löschen: Audio quittiert, aber der
        // Graveyard behält den Clip, solange der Pin steht
        clip->exportPins.fetch_add (1, std::memory_order_acq_rel);
        REQUIRE (bank.deleteClip (clip).wasOk());

        for (int i = 0; i < 6; ++i)
        {
            feed (1);
            bank.serviceMessageThread();
        }
        REQUIRE (bank.getRamBytesUsed() > 0);   // gepinnt → nicht freigegeben

        clip->exportPins.fetch_sub (1, std::memory_order_acq_rel);
        bank.serviceMessageThread();
        REQUIRE (bank.getRamBytesUsed() == 0);  // Unpin → Freigabe
    }

    folder.deleteRecursively();
}
