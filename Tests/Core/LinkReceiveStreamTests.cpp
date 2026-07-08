#include <atomic>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/LinkReceiveStream.h"

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr double kBpm        = 120.0;
    constexpr int    kBlock      = 128;
    constexpr float  kLatencyMs  = 100.0f;   // → 0.2 Beats → 4800 Frames @48k/120bpm

    constexpr double beatsPerFrame = kBpm / (60.0 * kSampleRate);
    constexpr int    latencyFrames = 4800;

    // Deterministische Quelle: Kanal 0 = Rampe, Kanal 1 = Konstante
    std::int16_t rampValue (std::int64_t frame, int channel) noexcept
    {
        return channel == 0 ? static_cast<std::int16_t> ((frame * 7) % 16384)
                            : static_cast<std::int16_t> (5000);
    }

    float toFloat (std::int16_t v) noexcept { return static_cast<float> (v) / 32768.0f; }

    conduit::ClockState clockAt (double beat)
    {
        conduit::ClockState clock;
        clock.bpm              = kBpm;
        clock.sampleRate       = kSampleRate;
        clock.beatAtBlockStart = beat;
        return clock;
    }

    /** Sender-Zustand: lückenloser Slot-Strom auf der Beat-Achse. */
    struct Feed
    {
        double        beat  = 0.0;
        std::uint64_t count = 1;
        std::int64_t  frame = 0;

        void pushSlot (conduit::LinkReceiveStream& stream, int numFrames,
                       double sampleRate = kSampleRate, double tempo = kBpm,
                       int numChannels = 1)
        {
            std::vector<std::int16_t> data (static_cast<size_t> (numFrames * numChannels));
            for (int i = 0; i < numFrames; ++i)
                for (int c = 0; c < numChannels; ++c)
                    data[static_cast<size_t> (i * numChannels + c)] = rampValue (frame + i, c);

            REQUIRE (stream.pushBuffer (data.data(), numFrames, numChannels,
                                        sampleRate, tempo, beat, count));
            beat  += (static_cast<double> (numFrames) / sampleRate) * (tempo / 60.0);
            frame += numFrames;
            ++count;
        }
    };
}

//==============================================================================
TEST_CASE ("LinkReceiveStream: Slot-Mathe und Push-Validierung", "[linkaudio][receive]")
{
    conduit::LinkReceiveSlot slot;
    slot.numFrames  = 128;
    slot.sampleRate = 48000.0;
    slot.tempo      = 120.0;
    slot.beatBegin  = 10.0;
    // 128 Frames @48k @120bpm = 128/48000*2 Beats
    REQUIRE (std::abs (slot.endBeat() - (10.0 + 128.0 / 24000.0)) < 1.0e-12);

    conduit::LinkReceiveStream stream;
    std::vector<std::int16_t> data (2048, 0);

    // Oversize (Frames × Kanäle > maxSamples) und ungültige Felder → Drop-Zähler
    REQUIRE_FALSE (stream.pushBuffer (data.data(), 1024, 2, 48000.0, 120.0, 0.0, 1));
    REQUIRE_FALSE (stream.pushBuffer (nullptr, 128, 1, 48000.0, 120.0, 0.0, 2));
    REQUIRE_FALSE (stream.pushBuffer (data.data(), 0, 1, 48000.0, 120.0, 0.0, 3));
    REQUIRE_FALSE (stream.pushBuffer (data.data(), 128, 1, 0.0, 120.0, 0.0, 4));
    REQUIRE (stream.getDroppedPushes() == 4u);

    // Sequenz-Lücke: 10 → 12 (11 fehlt)
    REQUIRE (stream.pushBuffer (data.data(), 128, 1, 48000.0, 120.0, 0.0, 10));
    REQUIRE (stream.pushBuffer (data.data(), 128, 1, 48000.0, 120.0, 0.0, 12));
    REQUIRE (stream.getSequenceGaps() == 1u);
}

//==============================================================================
TEST_CASE ("LinkReceiveStream: Queue-Überlauf droppt beim Producer", "[linkaudio][receive]")
{
    conduit::LinkReceiveStream stream;
    std::vector<std::int16_t> data (128, 0);

    int accepted = 0;
    for (std::uint64_t i = 1; i <= 200; ++i)
        if (stream.pushBuffer (data.data(), 128, 1, 48000.0, 120.0,
                               static_cast<double> (i) * 0.01, i))
            ++accepted;

    // Kapazität 128 (Zweierpotenz) — der Rest wird verworfen, nichts überschrieben
    REQUIRE (accepted == 128);
    REQUIRE (stream.getDroppedPushes() == 72u);
}

//==============================================================================
TEST_CASE ("LinkReceiveStream: beat-alignte Wiedergabe nach Latenzfenster", "[linkaudio][receive]")
{
    conduit::LinkReceiveStream stream;
    Feed feed;

    std::vector<float> left (kBlock), right (kBlock);

    bool sawIdleOrWaiting = false;
    int  firstStreamingBlock = -1;

    for (int block = 0; block < 90; ++block)
    {
        feed.pushSlot (stream, kBlock);
        stream.renderBlock (left.data(), right.data(), kBlock,
                            clockAt (static_cast<double> (block) * kBlock * beatsPerFrame),
                            kLatencyMs);

        const auto status = stream.getStatusForUi();
        if (status != conduit::LinkReceiveStream::Status::streaming)
            sawIdleOrWaiting = true;
        else if (firstStreamingBlock < 0)
            firstStreamingBlock = block;
    }

    REQUIRE (sawIdleOrWaiting);
    // Latenzfenster 4800 Frames → Start bei Block 38 (4864 ≥ 4800)
    REQUIRE (firstStreamingBlock == latencyFrames / kBlock + 1);
    REQUIRE (stream.getStatusForUi() == conduit::LinkReceiveStream::Status::streaming);

    // Wertprobe weit nach der Einstiegs-Fade-Phase (5 ms = 240 Frames):
    // Block 90 liefert Quell-Frames ab 90·128 − 4800
    {
        feed.pushSlot (stream, kBlock);
        const int block = 90;
        stream.renderBlock (left.data(), right.data(), kBlock,
                            clockAt (static_cast<double> (block) * kBlock * beatsPerFrame),
                            kLatencyMs);

        const std::int64_t srcStart = static_cast<std::int64_t> (block) * kBlock - latencyFrames;
        for (size_t i = 0; i < static_cast<size_t> (kBlock); i += 17)
        {
            const float expected = toFloat (rampValue (srcStart + static_cast<std::int64_t> (i), 0));
            REQUIRE (std::abs (left[i] - expected) < 1.0e-4f);
            REQUIRE (std::abs (right[i] - expected) < 1.0e-4f);   // mono → beide Kanäle
        }
    }

    // buffered ≈ Latenz (der Bestand hinter der Leseposition)
    REQUIRE (stream.getBufferedSeconds() > 0.08f);
    REQUIRE (stream.getBufferedSeconds() < 0.13f);
}

//==============================================================================
TEST_CASE ("LinkReceiveStream: Underflow → Stille + Reset, dann Wiederanlauf", "[linkaudio][receive]")
{
    conduit::LinkReceiveStream stream;
    Feed feed;

    std::vector<float> left (kBlock), right (kBlock);
    int block = 0;

    auto render = [&]
    {
        stream.renderBlock (left.data(), right.data(), kBlock,
                            clockAt (static_cast<double> (block) * kBlock * beatsPerFrame),
                            kLatencyMs);
        ++block;
    };

    for (int i = 0; i < 60; ++i)
    {
        feed.pushSlot (stream, kBlock);
        render();
    }
    REQUIRE (stream.getStatusForUi() == conduit::LinkReceiveStream::Status::streaming);
    REQUIRE (stream.getRenderResets() == 0u);

    // Feed stoppt — der Bestand (≈ Latenzfenster) trägt noch, dann Underflow
    for (int i = 0; i < 60; ++i)
        render();

    REQUIRE (stream.getStatusForUi() != conduit::LinkReceiveStream::Status::streaming);
    REQUIRE (stream.getRenderResets() >= 1u);

    // Ausklang ist declickt: aktueller Block ist (nahezu) still
    for (size_t i = 0; i < static_cast<size_t> (kBlock); ++i)
        REQUIRE (std::abs (left[i]) < 1.0e-3f);

    // Feed setzt an der aktuellen Beat-Achse wieder ein → Wiederanlauf
    feed.beat  = static_cast<double> (block) * kBlock * beatsPerFrame;
    feed.count += 100;   // bewusster Sprung — zählt als eine Lücke, stört sonst nicht

    for (int i = 0; i < 60; ++i)
    {
        feed.pushSlot (stream, kBlock);
        render();
    }
    REQUIRE (stream.getStatusForUi() == conduit::LinkReceiveStream::Status::streaming);
}

//==============================================================================
TEST_CASE ("LinkReceiveStream: fremde SampleRate wird re-pitcht (44.1k → 48k)", "[linkaudio][receive]")
{
    conduit::LinkReceiveStream stream;
    Feed feed;

    std::vector<float> left (kBlock), right (kBlock);

    // Peer sendet 44.1k in 147er-Slots (147/44100 = 128/38400 — krumm zum Block)
    for (int block = 0; block < 90; ++block)
    {
        feed.pushSlot (stream, 147, 44100.0);
        stream.renderBlock (left.data(), right.data(), kBlock,
                            clockAt (static_cast<double> (block) * kBlock * beatsPerFrame),
                            kLatencyMs);
    }

    REQUIRE (stream.getStatusForUi() == conduit::LinkReceiveStream::Status::streaming);

    // Rampe bleibt streng monoton (kein Wrap im Testfenster, keine Sprünge):
    // ein Alignment-Fehler würde Knicke oder Rückwärtssprünge erzeugen
    for (size_t i = 1; i < static_cast<size_t> (kBlock); ++i)
    {
        REQUIRE (std::isfinite (left[i]));
        REQUIRE (left[i] > left[i - 1] - 1.0e-4f);
    }
}

//==============================================================================
TEST_CASE ("LinkReceiveStream: Stereo-Mapping L/R", "[linkaudio][receive]")
{
    conduit::LinkReceiveStream stream;
    Feed feed;

    std::vector<float> left (kBlock), right (kBlock);

    for (int block = 0; block < 80; ++block)
    {
        feed.pushSlot (stream, kBlock, kSampleRate, kBpm, 2);
        stream.renderBlock (left.data(), right.data(), kBlock,
                            clockAt (static_cast<double> (block) * kBlock * beatsPerFrame),
                            kLatencyMs);
    }

    REQUIRE (stream.getStatusForUi() == conduit::LinkReceiveStream::Status::streaming);

    // Kanal 1 ist eine Konstante — rechts konstant, links Rampe (≠ konstant)
    const float expectedRight = toFloat (5000);
    for (size_t i = 0; i < static_cast<size_t> (kBlock); i += 13)
        REQUIRE (std::abs (right[i] - expectedRight) < 1.0e-4f);

    REQUIRE (std::abs (left[kBlock - 1] - left[0]) > 1.0e-3f);
}

//==============================================================================
TEST_CASE ("LinkReceiveStream: requestReset verwirft Bestand und Zustand", "[linkaudio][receive]")
{
    conduit::LinkReceiveStream stream;
    Feed feed;

    std::vector<float> left (kBlock), right (kBlock);

    for (int block = 0; block < 60; ++block)
    {
        feed.pushSlot (stream, kBlock);
        stream.renderBlock (left.data(), right.data(), kBlock,
                            clockAt (static_cast<double> (block) * kBlock * beatsPerFrame),
                            kLatencyMs);
    }
    REQUIRE (stream.getStatusForUi() == conduit::LinkReceiveStream::Status::streaming);

    stream.requestReset();   // Message Thread: Source-/Kanal-Wechsel

    stream.renderBlock (left.data(), right.data(), kBlock,
                        clockAt (60.0 * kBlock * beatsPerFrame), kLatencyMs);

    REQUIRE (stream.getStatusForUi() == conduit::LinkReceiveStream::Status::idle);
    REQUIRE (stream.getBufferedSeconds() < 1.0e-9f);
}

//==============================================================================
TEST_CASE ("LinkReceiveStream: Producer/Consumer-Stress über echte Threads (TSan)",
           "[linkaudio][receive][threading]")
{
    conduit::LinkReceiveStream stream;
    std::atomic<bool> stop { false };

    // Producer = Link-Thread-Rolle. Catch2-Assertions sind nicht thread-safe —
    // der Thread prüft nichts, TSan prüft die Queue-Grenze.
    std::thread producer ([&stream, &stop]
    {
        std::int16_t data[kBlock];
        for (int i = 0; i < kBlock; ++i)
            data[i] = static_cast<std::int16_t> (i * 13);

        double        beat  = 0.0;
        std::uint64_t count = 1;

        while (! stop.load (std::memory_order_relaxed))
        {
            stream.pushBuffer (data, kBlock, 1, kSampleRate, kBpm, beat, count);
            beat += static_cast<double> (kBlock) * beatsPerFrame;
            ++count;
            std::this_thread::yield();
        }
    });

    std::vector<float> left (kBlock), right (kBlock);
    double localBeat = 0.0;

    for (int block = 0; block < 3000; ++block)
    {
        stream.renderBlock (left.data(), right.data(), kBlock,
                            clockAt (localBeat), 50.0f);
        localBeat += static_cast<double> (kBlock) * beatsPerFrame;

        if ((block % 500) == 0)
            stream.requestReset();   // Message-Thread-Pfad mit-stressen
    }

    stop.store (true, std::memory_order_relaxed);
    producer.join();

    for (size_t i = 0; i < static_cast<size_t> (kBlock); ++i)
        REQUIRE (std::isfinite (left[i]));
}
