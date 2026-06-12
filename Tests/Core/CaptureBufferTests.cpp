#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "Core/Capture/CaptureService.h"

using conduit::BufferPool;
using conduit::CaptureChannel;
using conduit::CaptureRingBuffer;
using conduit::CaptureService;
using conduit::CaptureSettings;
using conduit::PreRollBuffer;

namespace
{

/** Eindeutiger Sample-Wert pro absoluter Position — exakt als float
    darstellbar (Werte < 2^24), Vergleich via juce::exactlyEqual. */
float rampValue (std::uint64_t position) noexcept
{
    return static_cast<float> (position % 1'000'003ull);
}

/** Service-Tests skalieren die Rampe unter die Gate-Schwelle: seit
    Baustein 4 detektiert der Tap automatisch (RMS über effektiver
    Schwelle öffnet das Gate) — diese Tests steuern die Gates aber gezielt
    über die manuelle Seam. 2^-30 hält den Pegel ≤ −60 dBFS (deutlich
    unter Threshold −40 dB) und verschiebt nur den Float-Exponenten:
    die Werte bleiben exakt vergleichbar. */
constexpr float serviceRampScale = 1.0f / static_cast<float> (1 << 30);

void fillRamp (float* dest, int numSamples, std::uint64_t startPosition, float scale = 1.0f)
{
    for (int i = 0; i < numSamples; ++i)
        dest[i] = scale * rampValue (startPosition + static_cast<std::uint64_t> (i));
}

/** Liest [from, to) aus dem Kanal und zählt Abweichungen von der Rampe.
    -1 wenn der Bereich nicht lesbar ist. */
int countRampMismatches (const CaptureChannel& channel, std::uint64_t from, std::uint64_t to,
                         float scale = 1.0f)
{
    const auto length = static_cast<int> (to - from);
    std::vector<float> samples (static_cast<size_t> (length));

    if (! channel.read (from, samples.data(), length))
        return -1;

    int mismatches = 0;
    for (int i = 0; i < length; ++i)
        if (! juce::exactlyEqual (samples[static_cast<size_t> (i)],
                                  scale * rampValue (from + static_cast<std::uint64_t> (i))))
            ++mismatches;
    return mismatches;
}

/** Kanal-Prüfstand: spielt den Audio Thread (process VOR PreRoll-write —
    dieselbe Reihenfolge wie CaptureService::processInputTap). */
struct ChannelHarness
{
    ChannelHarness (int preRollCapacity, int segmentSamples, int budget, int blockSize)
        : blockSizeSamples (blockSize)
    {
        preRoll.prepare (preRollCapacity);
        pool.prepare (segmentSamples, 2, 1);
        channel.prepare (preRoll, pool, budget);
        block.resize (static_cast<size_t> (blockSize));
    }

    void feedBlock()
    {
        fillRamp (block.data(), blockSizeSamples, position);
        channel.process (block.data(), blockSizeSamples, position);
        preRoll.write (block.data(), blockSizeSamples, position);
        position += static_cast<std::uint64_t> (blockSizeSamples);
    }

    void feedBlocks (int count)
    {
        for (int i = 0; i < count; ++i)
            feedBlock();
    }

    PreRollBuffer preRoll;
    BufferPool pool;
    CaptureChannel channel;
    std::vector<float> block;
    int blockSizeSamples;
    std::uint64_t position = 0;
};

/** Settings-Persistenz in ein Temp-Verzeichnis statt in die echte
    Conduit.settings des Users — Verzeichnis wird im Dtor gelöscht. */
struct TempCaptureSettings
{
    TempCaptureSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitCaptureBufferTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();

        juce::PropertiesFile::Options options;
        options.applicationName = "ConduitCaptureBufferTests";
        options.filenameSuffix  = ".settings";
        options.folderName      = folder.getFullPathName();  // absoluter Pfad
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

/** Füllt alle Kanäle eines Buffers mit der (leisen) Rampe und füttert den
    Tap — unter der Gate-Schwelle, damit die Auto-Detektion stumm bleibt. */
void feedServiceBlocks (CaptureService& service, juce::AudioBuffer<float>& buffer,
                        int count, std::uint64_t& position)
{
    for (int i = 0; i < count; ++i)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            fillRamp (buffer.getWritePointer (ch), buffer.getNumSamples(), position,
                      serviceRampScale);

        service.processInputTap (buffer, buffer.getNumChannels());
        position += static_cast<std::uint64_t> (buffer.getNumSamples());
    }
}

} // namespace

//==============================================================================
TEST_CASE ("PreRollBuffer: Wraparound, absolute Positionen, Resync nach Sprung", "[capture]")
{
    PreRollBuffer preRoll;
    preRoll.prepare (1000);
    REQUIRE (preRoll.getCapacity() == 1000);
    REQUIRE (preRoll.getValidSamples() == 0);

    // 3000 Samples in 128er-Blöcken: dreifacher Wrap
    std::vector<float> block (128);
    std::uint64_t position = 0;
    while (position < 3000)
    {
        fillRamp (block.data(), 128, position);
        preRoll.write (block.data(), 128, position);
        position += 128;
    }

    REQUIRE (preRoll.getEndPosition() == position);
    REQUIRE (preRoll.getValidSamples() == 1000);
    REQUIRE (preRoll.getOldestValidPosition() == position - 1000);

    SECTION ("Spans über die Wrap-Grenze liefern die richtigen Werte")
    {
        const auto from = preRoll.getOldestValidPosition();
        const auto spans = preRoll.makeReadSpans (from, 1000);
        REQUIRE (spans.num1 + spans.num2 == 1000);

        int mismatches = 0;
        for (int i = 0; i < spans.num1; ++i)
            if (! juce::exactlyEqual (spans.data1[i], rampValue (from + static_cast<std::uint64_t> (i))))
                ++mismatches;
        for (int i = 0; i < spans.num2; ++i)
            if (! juce::exactlyEqual (spans.data2[i],
                                      rampValue (from + static_cast<std::uint64_t> (spans.num1 + i))))
                ++mismatches;
        REQUIRE (mismatches == 0);
    }

    SECTION ("Positionssprung verwirft die Historie (Set-Swap, Aussetzer)")
    {
        fillRamp (block.data(), 128, position + 500);  // Lücke von 500 Samples
        preRoll.write (block.data(), 128, position + 500);

        REQUIRE (preRoll.getValidSamples() == 128);
        REQUIRE (preRoll.getOldestValidPosition() == position + 500);
        REQUIRE (preRoll.getEndPosition() == position + 628);
    }

    SECTION ("clear() verwirft den Fill-State, Position bleibt")
    {
        preRoll.clear();
        REQUIRE (preRoll.getValidSamples() == 0);
        REQUIRE (preRoll.getOldestValidPosition() == preRoll.getEndPosition());
    }
}

//==============================================================================
TEST_CASE ("CaptureRingBuffer: positionsadressiertes Schreiben mit Wrap", "[capture]")
{
    juce::HeapBlock<float> storage (1000);
    CaptureRingBuffer ring;
    REQUIRE_FALSE (ring.hasStorage());

    ring.attach (storage.get(), 1000, 500);  // Anker bei Position 500
    REQUIRE (ring.hasStorage());
    REQUIRE (ring.getStartSamplePosition() == 500);
    REQUIRE (ring.getEndPosition() == 500);

    // 2500 Samples sequentiell — Sample p landet bei (p - 500) % 1000
    std::vector<float> block (128);
    std::uint64_t position = 500;
    while (position < 3000)
    {
        const auto todo = static_cast<int> (juce::jmin (std::uint64_t { 128 }, 3000 - position));
        fillRamp (block.data(), todo, position);
        ring.writeAt (position, block.data(), todo);
        position += static_cast<std::uint64_t> (todo);
        ring.publishEnd (position);
    }

    // Die letzten 1000 Samples [2000, 3000) sind die gültige Belegung
    std::vector<float> out (1000);
    ring.copyOut (2000, out.data(), 1000);

    int mismatches = 0;
    for (int i = 0; i < 1000; ++i)
        if (! juce::exactlyEqual (out[static_cast<size_t> (i)],
                                  rampValue (2000 + static_cast<std::uint64_t> (i))))
            ++mismatches;
    REQUIRE (mismatches == 0);

    auto* released = ring.detach();
    REQUIRE (released == storage.get());
    REQUIRE_FALSE (ring.hasStorage());
}

//==============================================================================
TEST_CASE ("CaptureChannel: Pre-Roll-Übernahme sample-genau", "[capture]")
{
    // Pre-Roll 6000, Fenster 3000: genug Headroom, dass die Pool-Brücke
    // den ältesten Rand NICHT anknabbert — der Start ist exakt
    ChannelHarness h (6000, 40000, 512, 128);

    h.feedBlocks (40);  // 5120 Samples Vorlauf
    REQUIRE (h.channel.getState() == CaptureChannel::State::idle);

    const auto gateOpen = h.position;
    h.channel.openGate (gateOpen, 3000);
    REQUIRE (h.channel.getState() == CaptureChannel::State::awaitingSegment);

    // Pool-Brücke: 5 Blöcke (640 Samples) ohne Segment — nur der Pre-Roll
    // hält die Live-Daten fest
    h.feedBlocks (5);
    REQUIRE (h.channel.getState() == CaptureChannel::State::awaitingSegment);

    h.pool.service();  // [Message Thread] publiziert das vorgehaltene Segment
    h.feedBlock();     // Claim + Übernahme beginnt + Live-Schreiben
    REQUIRE (h.channel.getState() == CaptureChannel::State::recording);

    // Start exakt Gate-Open minus Fenster — die Brücke ging zulasten des
    // Headrooms, nicht des Fensters
    REQUIRE (h.channel.getStartSamplePosition() == gateOpen - 3000);

    int blocksUntilComplete = 0;
    while (! h.channel.isTakeoverComplete() && blocksUntilComplete < 100)
    {
        h.feedBlock();
        ++blocksUntilComplete;
    }
    REQUIRE (h.channel.isTakeoverComplete());

    // Gesamter Bereich inkl. Brücken-Samples sample-genau
    const auto range = h.channel.getReadableRange();
    REQUIRE (range.from == gateOpen - 3000);
    REQUIRE (range.to == h.position);
    REQUIRE (countRampMismatches (h.channel, range.from, range.to) == 0);

    SECTION ("Wiedereröffnung innerhalb des Fensters: nahtlose Fortsetzung")
    {
        h.channel.closeGate (h.position);
        REQUIRE (h.channel.getState() == CaptureChannel::State::held);
        REQUIRE (h.channel.getHeldSincePosition() == h.position);

        h.feedBlocks (10);  // Pause 1280 Samples < Fenster 3000

        h.channel.openGate (h.position, 3000);
        REQUIRE (h.channel.getState() == CaptureChannel::State::recording);
        REQUIRE (h.channel.getStartSamplePosition() == gateOpen - 3000);  // Anker bleibt

        int guard = 0;
        while (! h.channel.isTakeoverComplete() && guard++ < 100)
            h.feedBlock();
        REQUIRE (h.channel.isTakeoverComplete());

        // Auch die Gate-Pause ist lückenlos da (rückwirkend aus dem Pre-Roll)
        const auto reopened = h.channel.getReadableRange();
        REQUIRE (reopened.from == gateOpen - 3000);
        REQUIRE (reopened.to == h.position);
        REQUIRE (countRampMismatches (h.channel, reopened.from, reopened.to) == 0);
    }

    SECTION ("Wiedereröffnung nach langer Pause: neuer Anker, altes Material verfällt")
    {
        h.channel.closeGate (h.position);
        h.feedBlocks (60);  // Pause 7680 Samples > Fenster 3000

        const auto reopenAt = h.position;
        h.channel.openGate (reopenAt, 3000);
        REQUIRE (h.channel.getState() == CaptureChannel::State::recording);
        REQUIRE (h.channel.getStartSamplePosition() == reopenAt - 3000);

        int guard = 0;
        while (! h.channel.isTakeoverComplete() && guard++ < 100)
            h.feedBlock();
        REQUIRE (h.channel.isTakeoverComplete());

        const auto reanchored = h.channel.getReadableRange();
        REQUIRE (reanchored.from == reopenAt - 3000);
        REQUIRE (countRampMismatches (h.channel, reanchored.from, reanchored.to) == 0);
    }

    SECTION ("releaseStorage gibt das Segment an den Pool zurück")
    {
        h.channel.closeGate (h.position);
        h.channel.requestRelease();   // [Message Thread] RAM-Wächter
        h.feedBlock();                // Audio quittiert

        REQUIRE (h.channel.getState() == CaptureChannel::State::idle);
        const auto range2 = h.channel.getReadableRange();
        REQUIRE (range2.from == range2.to);
    }
}

//==============================================================================
TEST_CASE ("CaptureChannel: Amortisierung terminiert im Budget", "[capture]")
{
    // Härtester Fall: Fenster == Pre-Roll-Kapazität, kein Headroom — der
    // Übernahme-Cursor startet exakt auf dem ältesten gültigen Sample und
    // das Budget (4×Block) muss dem nachrückenden Pre-Roll davonlaufen
    constexpr int blockSize = 64;
    constexpr int window    = 4096;
    ChannelHarness h (window, 16384, 4 * blockSize, blockSize);

    h.feedBlocks (125);  // 8000 Samples — Pre-Roll voll, mehrfach gewrappt

    const auto gateOpen = h.position;
    h.channel.openGate (gateOpen, window);

    h.feedBlock();      // Brücke: 1 Block
    h.pool.service();
    h.feedBlock();      // Claim — Übernahme beginnt
    const auto attachStart = h.channel.getStartSamplePosition();

    // Fenster == Kapazität: die Brücke knabbert am ältesten Rand —
    // der Start liegt exakt ein Fenster hinter dem Attach-Block
    REQUIRE (attachStart == h.position - static_cast<std::uint64_t> (window + blockSize));

    // Budget 4×Block über pending ≈ window: ceil(4096 / 256) = 16 Blöcke
    int blocks = 0;
    while (! h.channel.isTakeoverComplete() && blocks < 17)
    {
        h.feedBlock();
        ++blocks;
    }
    REQUIRE (h.channel.isTakeoverComplete());
    REQUIRE (blocks <= 16);

    // Sample-genau über den gesamten Bereich — beweist, dass die Übernahme
    // nie von bereits überschriebenen Pre-Roll-Samples gelesen hat
    const auto range = h.channel.getReadableRange();
    REQUIRE (range.from == attachStart);
    REQUIRE (range.to == h.position);
    REQUIRE (countRampMismatches (h.channel, range.from, range.to) == 0);
}

//==============================================================================
TEST_CASE ("BufferPool: RAM-Limit, Vorhalteziel und Surplus-Segmente", "[capture]")
{
    BufferPool pool;
    pool.prepare (1000, 2, 1);
    REQUIRE (pool.getNumSegments() == 1);  // Vorhalteziel
    REQUIRE (pool.getAllocatedBytes() == 1000 * static_cast<std::int64_t> (sizeof (float)));

    // Zwei Anforderungen, aber nur zwei Segmente Budget
    pool.requestSegment();
    pool.requestSegment();
    pool.requestSegment();
    pool.service();

    auto* first  = pool.tryClaimSegment();
    auto* second = pool.tryClaimSegment();
    REQUIRE (first != nullptr);
    REQUIRE (second != nullptr);
    REQUIRE (pool.tryClaimSegment() == nullptr);
    REQUIRE (pool.isExhausted());           // dritte Anforderung unbedienbar
    REQUIRE (pool.getNumSegments() == 2);   // maxSegments respektiert

    // Rückgabe → service bedient die wartende Anforderung mit dem Rückläufer
    pool.releaseSegment (first);
    pool.service();
    REQUIRE_FALSE (pool.isExhausted());
    auto* third = pool.tryClaimSegment();
    REQUIRE (third == first);  // Segmente sind austauschbar

    // Alles zurück → Überschuss wird auf das Vorhalteziel abgebaut
    pool.releaseSegment (second);
    pool.releaseSegment (third);
    pool.service();
    REQUIRE (pool.getNumSegments() == 1);
}

//==============================================================================
TEST_CASE ("BufferPool: Audio/Message-Handshake unter Threads (TSan-Ziel)", "[capture]")
{
    BufferPool pool;
    constexpr int segmentSamples = 2048;
    pool.prepare (segmentSamples, 4, 1);

    constexpr int iterations = 500;
    std::atomic<bool> audioDone { false };
    std::atomic<int> claims { 0 };
    std::atomic<int> dataErrors { 0 };

    // "Audio Thread": anfordern, abholen, Segment beschreiben/verifizieren,
    // zurückgeben — kein Catch2-Makro außerhalb des Haupt-Threads
    std::thread audioThread ([&pool, &audioDone, &claims, &dataErrors]
    {
        for (int i = 0; i < iterations; ++i)
        {
            pool.requestSegment();

            float* segment = nullptr;
            while ((segment = pool.tryClaimSegment()) == nullptr)
                std::this_thread::yield();

            // Speicher-Übergabe validieren: schreiben und zurücklesen —
            // TSan prüft die Sichtbarkeit über die Queue-Grenzen
            const auto stamp = static_cast<float> (i);
            for (int s = 0; s < segmentSamples; s += 256)
                segment[s] = stamp;
            for (int s = 0; s < segmentSamples; s += 256)
                if (! juce::exactlyEqual (segment[s], stamp))
                    dataErrors.fetch_add (1, std::memory_order_relaxed);

            pool.releaseSegment (segment);
            claims.fetch_add (1, std::memory_order_relaxed);
        }

        audioDone.store (true, std::memory_order_release);
    });

    // "Message Thread": service-Schleife inkl. Allokation/Freigabe
    std::thread messageThread ([&pool, &audioDone]
    {
        while (! audioDone.load (std::memory_order_acquire))
        {
            pool.service();
            std::this_thread::yield();
        }
        pool.service();  // letzte Rückläufer einsammeln
    });

    audioThread.join();
    messageThread.join();

    REQUIRE (claims.load() == iterations);
    REQUIRE (dataErrors.load() == 0);

    pool.service();
    REQUIRE (pool.getNumSegments() == 1);  // zurück auf dem Vorhalteziel
}

//==============================================================================
TEST_CASE ("CaptureService: Gate → Pool → Aufnahme end-to-end mit Resize-Policy", "[capture]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;
    CaptureService service (*temp.settings);
    temp.settings->setBufferHost (&service);

    service.prepare (1000.0, 32, 2);  // niedrige Samplerate hält den Test klein
    REQUIRE (service.getRingCapacitySamples() == 900'000);
    REQUIRE (service.getRingNumChannels() == 2);

    juce::AudioBuffer<float> buffer (2, 32);
    std::uint64_t position = 0;

    feedServiceBlocks (service, buffer, 100, position);  // 3.2 s Vorlauf
    REQUIRE_FALSE (service.isAnyChannelActive());

    // Gate öffnen [Audio-Seam] — Kanal aktiv, Resize geht in Pending
    service.openGate (0);
    feedServiceBlocks (service, buffer, 1, position);
    REQUIRE (service.isAnyChannelActive());
    REQUIRE (temp.settings->setBufferMinutes (20)
             == CaptureSettings::ResizeOutcome::pendingConfirmation);
    REQUIRE (service.getRingCapacitySamples() == 900'000);  // unverändert

    // Pool bedienen (sonst RAM-Wächter-Timer) → Aufnahme läuft an
    service.runRamGuard();
    feedServiceBlocks (service, buffer, 1, position);
    const auto* channel = service.getChannel (0);
    REQUIRE (channel != nullptr);
    REQUIRE (channel->getState() == CaptureChannel::State::recording);

    // Gate lag vor dem Pre-Roll-Fenster-Ende → Start auf ältestem Bestand
    int guard = 0;
    while (! channel->isTakeoverComplete() && guard++ < 100)
        feedServiceBlocks (service, buffer, 1, position);
    REQUIRE (channel->isTakeoverComplete());

    const auto range = channel->getReadableRange();
    REQUIRE (range.to == position);
    REQUIRE (countRampMismatches (*channel, range.from, range.to, serviceRampScale) == 0);

    // Gate zu → held zählt weiterhin als aktiv (Resize-Policy)
    service.closeGate (0);
    feedServiceBlocks (service, buffer, 1, position);
    REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::held);
    REQUIRE (service.isAnyChannelActive());

    // Bestätigung: invalidieren → übernehmen → reallokieren (neuer Puffersatz)
    temp.settings->confirmPendingResize();
    REQUIRE_FALSE (service.isAnyChannelActive());
    REQUIRE (service.getRingCapacitySamples() == 20 * 60 * 1000);

    // Audio übernimmt den neuen Satz aus der Mailbox und quittiert den alten
    feedServiceBlocks (service, buffer, 1, position);
    REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::idle);

    // Guard-Tick sammelt den quittierten Satz ein — SampleClock lief durch
    service.runRamGuard();
    REQUIRE (service.getSampleClock().now() == position);
}

//==============================================================================
TEST_CASE ("CaptureService: RAM-Wächter gibt den ältesten gehaltenen Kanal frei", "[capture]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;

    // 30-min-Ringe bei 48 kHz und 1 GB Limit: Platz für 2 Segmente bei
    // 4 Kanälen (Pool allokiert uninitialisiert — nur virtueller Speicher)
    REQUIRE (temp.settings->setBufferMinutes (30) == CaptureSettings::ResizeOutcome::applied);
    temp.settings->setRamLimitGb (1);

    CaptureService service (*temp.settings);
    service.prepare (48000.0, 480, 4);

    const auto segmentBytes = static_cast<std::int64_t> (service.getRingCapacitySamples())
                            * static_cast<std::int64_t> (sizeof (float));
    const auto maxSegments = CaptureService::computeMaxSegments (
        1, segmentBytes,
        4 * static_cast<std::int64_t> (
                CaptureService::computePreRollCapacitySamples (60, 48000.0))
          * static_cast<std::int64_t> (sizeof (float)),
        4);
    REQUIRE (maxSegments >= 2);
    REQUIRE (maxSegments < 4);  // sonst testet der Fall nichts

    juce::AudioBuffer<float> buffer (4, 480);
    std::uint64_t position = 0;
    feedServiceBlocks (service, buffer, 10, position);

    // maxSegments Kanäle nacheinander aufnehmen und halten
    for (int ch = 0; ch < maxSegments; ++ch)
    {
        service.openGate (ch);
        feedServiceBlocks (service, buffer, 2, position);
        service.runRamGuard();
        feedServiceBlocks (service, buffer, 2, position);
        REQUIRE (service.getChannel (ch)->getState() == CaptureChannel::State::recording);

        service.closeGate (ch);
        feedServiceBlocks (service, buffer, 1, position);
        REQUIRE (service.getChannel (ch)->getState() == CaptureChannel::State::held);
    }
    REQUIRE_FALSE (service.isRamWarningActive());

    // Ein weiterer Kanal: Pool ist ausgehungert → Wächter warnt und gibt
    // den ÄLTESTEN gehaltenen Kanal (0) frei
    const int extraChannel = maxSegments;
    service.openGate (extraChannel);
    feedServiceBlocks (service, buffer, 1, position);

    service.runRamGuard();
    REQUIRE (service.isRamWarningActive());

    feedServiceBlocks (service, buffer, 1, position);  // Audio quittiert die Freigabe
    REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::idle);
    REQUIRE (service.getChannel (1)->getState() == CaptureChannel::State::held);

    // Nächster Tick: Rückläufer bedient die wartende Anforderung
    service.runRamGuard();
    feedServiceBlocks (service, buffer, 1, position);
    REQUIRE (service.getChannel (extraChannel)->getState()
             == CaptureChannel::State::recording);

    // Entwarnung, sobald der Pool wieder liefert
    service.runRamGuard();
    REQUIRE_FALSE (service.isRamWarningActive());
}
