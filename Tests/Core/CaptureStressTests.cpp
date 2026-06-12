#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "Core/Capture/CaptureService.h"
#include "Util/RtAllocationGuard.h"

// Stress- und Threading-Tests des Capture-Systems:
//   - RT-Audit des Input-Tap-Pfads (RtAllocationGuard, unter Last)
//   - 16 Kanäle, voller 15-min-Ring, Export bei laufender Aufnahme
//   - Device-/Samplerate-Wechsel mit offenem Gate (Auto-Export-Sicherheitsnetz)
//   - RAM-Wächter räumt ausschließlich gehaltene (inaktive) Kanäle
//   - Export-Halte-Protokoll unter echter Nebenläufigkeit
//
// Threading-Muster wie Tests/Core/ThreadingStressTests.cpp: echte Threads
// für Pool-Handshake und Writer-Snapshot, primär für die Sanitizer-Presets
// (TSan-Pflicht für Cross-Thread-Code, CLAUDE.md 13.4); kein Catch2-Makro
// außerhalb des Haupt-Threads. Alle Tests laufen OHNE Audio-Device
// (Repo-Konvention: der Tap wird direkt gefüttert) — CI-tauglich.

using conduit::BufferPool;
using conduit::CaptureChannel;
using conduit::CaptureService;
using conduit::CaptureSettings;
using conduit::PreRollBuffer;

namespace
{

/** Eindeutiger Sample-Wert pro absoluter Position — exakt als float
    darstellbar (Werte < 2^20), Vergleich via juce::exactlyEqual. */
float rampValue (std::uint64_t position) noexcept
{
    return static_cast<float> (position % 1'000'003ull);
}

/** Service-Tests skalieren die Rampe unter die Gate-Schwelle (≤ −60 dBFS,
    Threshold-Default −40 dB) — die Gates werden gezielt über die manuelle
    Seam gesteuert. 2^-30 verschiebt nur den Float-Exponenten: die Werte
    bleiben exakt vergleichbar, auch durch einen 32-bit-Float-WAV-Roundtrip. */
constexpr float serviceRampScale = 1.0f / static_cast<float> (1 << 30);

void fillRamp (float* dest, int numSamples, std::uint64_t startPosition, float scale = 1.0f)
{
    for (int i = 0; i < numSamples; ++i)
        dest[i] = scale * rampValue (startPosition + static_cast<std::uint64_t> (i));
}

/** Settings-Persistenz in ein Temp-Verzeichnis statt in die echte
    Conduit.settings des Users — Verzeichnis wird im Dtor gelöscht. */
struct TempCaptureSettings
{
    TempCaptureSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitCaptureStressTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();

        juce::PropertiesFile::Options options;
        options.applicationName = "ConduitCaptureStressTests";
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

/** Zeitlich begrenztes Warten (statt Spin-Zähler: die Predicates hier sind
    teuer, z.B. runRamGuard) — hängt nie, auch wenn etwas bricht. */
template <typename Predicate>
bool waitUntil (Predicate&& predicate, int timeoutMs = 30'000)
{
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds (timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
            return true;

        std::this_thread::yield();
    }

    return false;
}

/** Liest eine Mono-WAV vollständig zurück (+ BWF-TimeReference und Rate). */
juce::AudioBuffer<float> readWavFile (const juce::File& file, juce::String& timeReference,
                                      double& sampleRateOut)
{
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader (
        format.createReaderFor (file.createInputStream().release(), true));

    juce::AudioBuffer<float> buffer;
    if (reader == nullptr)
        return buffer;

    timeReference = reader->metadataValues.getValue (juce::WavAudioFormat::bwavTimeReference, "");
    sampleRateOut = reader->sampleRate;
    buffer.setSize (1, static_cast<int> (reader->lengthInSamples));
    reader->read (&buffer, 0, static_cast<int> (reader->lengthInSamples), 0, true, false);
    return buffer;
}

} // namespace

//==============================================================================
TEST_CASE ("RtAllocationGuard: Section-Flag und Violation-Zähler", "[capture][stress][rt]")
{
    REQUIRE_FALSE (conduit::rt::isRealtimeSection());

    // Flag-Semantik prüfen — Ergebnisse erst NACH den Sections asserten
    // (Catch2-Makros allokieren und würden selbst als Violation zählen)
    bool activeInside = false;
    bool activeInAllowance = false;
    bool activeAfterAllowance = false;

    const auto before = conduit::rt::getAllocationViolations();
    {
        const conduit::rt::ScopedRealtimeSection section;
        activeInside = conduit::rt::isRealtimeSection();
        {
            const conduit::rt::ScopedAllocationAllowance allowance;
            activeInAllowance = conduit::rt::isRealtimeSection();
        }
        activeAfterAllowance = conduit::rt::isRealtimeSection();
    }

    REQUIRE (activeInside);
    REQUIRE_FALSE (activeInAllowance);
    REQUIRE (activeAfterAllowance);
    REQUIRE_FALSE (conduit::rt::isRealtimeSection());
    REQUIRE (conduit::rt::getAllocationViolations() == before);  // nichts allokiert

   #if CONDUIT_RT_ALLOCATION_CHECKS
    SECTION ("Hook zählt new/delete innerhalb einer Section (Dev-Build)")
    {
        // Hinweis: unter einem ANGEHÄNGTEN Debugger hält die absichtliche
        // Violation hier per __debugbreak an — gewolltes Audit-Verhalten.
        const auto baseline = conduit::rt::getAllocationViolations();
        {
            const conduit::rt::ScopedRealtimeSection section;
            auto* violation = new int (42);
            delete violation;
        }
        REQUIRE (conduit::rt::getAllocationViolations() >= baseline + 2);  // new + delete

        const auto afterViolation = conduit::rt::getAllocationViolations();
        {
            const conduit::rt::ScopedRealtimeSection section;
            const conduit::rt::ScopedAllocationAllowance allowance;
            auto* allowed = new int (7);
            delete allowed;
        }
        REQUIRE (conduit::rt::getAllocationViolations() == afterViolation);
    }
   #endif
}

//==============================================================================
TEST_CASE ("CaptureService-Stress: 16 Kanäle, voller 15-min-Ring, Export bei laufender Aufnahme",
           "[capture][stress][threading]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;

    constexpr int numChannels = 16;
    constexpr int blockSize = 480;
    constexpr double sampleRate = 500.0;  // hält den VOLLEN 15-min-Ring testbar:
                                          // 450k Samples/Kanal statt 43M bei 48 kHz

    const auto exportDir = temp.folder.getChildFile ("exports");
    REQUIRE (exportDir.createDirectory());
    temp.settings->setExportDirectory (exportDir);
    temp.settings->setExportBitDepth (32);  // Float-WAV: Roundtrip bitexakt
    REQUIRE (temp.settings->getExportDirectory() == exportDir);

    CaptureService service (*temp.settings);
    temp.settings->setBufferHost (&service);
    service.prepare (sampleRate, blockSize, numChannels);

    const auto ringCapacity = service.getRingCapacitySamples();
    REQUIRE (ringCapacity == 15 * 60 * 500);  // 15-min-Puffer, ungekürzt
    REQUIRE (service.getRingNumChannels() == numChannels);

    const auto rtBaseline = conduit::rt::getAllocationViolations();

    std::atomic<bool> stopFeeder { false };
    std::atomic<bool> exportTriggered { false };
    std::atomic<bool> feederDone { false };

    // Simulierter Audio Thread (Muster ThreadingStressTests): füttert den
    // Tap ununterbrochen unter RT-Audit, öffnet die Gates im ersten Block
    // (Audio-Seam) und läuft nach dem Export-Trigger noch begrenzt weiter —
    // 64 Blöcke ≈ 30k Samples, weit unter der Überholschutz-Marge
    // (Kapazität/8 = 56k), damit der Writer-Snapshot nie überholt wird.
    std::thread audioThread ([&]
    {
        juce::AudioBuffer<float> buffer (numChannels, blockSize);
        std::uint64_t position = 0;
        bool gatesOpened = false;
        int blocksAfterExport = 0;

        while (! stopFeeder.load (std::memory_order_acquire))
        {
            for (int ch = 0; ch < numChannels; ++ch)
                fillRamp (buffer.getWritePointer (ch), blockSize, position, serviceRampScale);

            {
                const conduit::rt::ScopedRealtimeSection rtAudit;
                service.processInputTap (buffer, numChannels);

                if (! gatesOpened)
                {
                    for (int ch = 0; ch < numChannels; ++ch)
                        service.openGate (ch);
                    gatesOpened = true;
                }
            }

            position += static_cast<std::uint64_t> (blockSize);

            if (exportTriggered.load (std::memory_order_acquire)
                && ++blocksAfterExport >= 64)
                break;

            std::this_thread::yield();
        }

        feederDone.store (true, std::memory_order_release);
    });

    // [MT] Guard-Ticks bedienen den Pool nebenläufig (das ist der
    // Pool-Handshake unter echten Threads), bis alle 16 Kanäle aufnehmen
    // und die Pre-Roll-Übernahme abgeschlossen ist
    auto allChannelsRecording = [&service]
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* channel = service.getChannel (ch);
            if (channel == nullptr
                || channel->getState() != CaptureChannel::State::recording
                || ! channel->isTakeoverComplete())
                return false;
        }
        return true;
    };
    REQUIRE (waitUntil ([&] { service.runRamGuard(); return allChannelsRecording(); }));

    // Ring volllaufen lassen: lesbarer Bereich == Kapazität (die Ringe und
    // Pre-Rolls sind dann mehrfach gewrappt)
    auto allRingsFull = [&]
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto range = service.getChannel (ch)->getReadableRange();
            if (range.to - range.from < static_cast<std::uint64_t> (ringCapacity))
                return false;
        }
        return true;
    };
    REQUIRE (waitUntil ([&] { service.runRamGuard(); return allRingsFull(); }, 120'000));

    // Export bei LAUFENDER Aufnahme — alle 16 Spuren in einem Job; der
    // Feeder schreibt parallel weiter (Writer-Snapshot vs. Producer)
    const auto numQueued = service.exportAll();
    REQUIRE (numQueued == numChannels);
    exportTriggered.store (true, std::memory_order_release);

    REQUIRE (waitUntil ([&] { service.runRamGuard(); return ! service.isExportBusy(); },
                        120'000));
    REQUIRE (waitUntil ([&] { return feederDone.load (std::memory_order_acquire); }));
    stopFeeder.store (true, std::memory_order_release);
    audioThread.join();

    // RT-Audit: kein new/delete im gesamten Tap-Pfad unter Volllast
    // (16 Kanäle × Gate + Pre-Roll + Übernahme + Ring + Satz-Handoff)
    REQUIRE (conduit::rt::getAllocationViolations() == rtBaseline);

    // 16 Dateien, eine pro Input-Kanal, alle Take 1
    auto files = exportDir.findChildFiles (juce::File::findFiles, false, "*.wav");
    REQUIRE (files.size() == numChannels);

    auto findTrackFile = [&files] (const juce::String& trackToken)
    {
        for (const auto& file : files)
            if (file.getFileName().contains (trackToken))
                return file;
        return juce::File();
    };

    for (int ch = 1; ch <= numChannels; ++ch)
        REQUIRE (findTrackFile ("_in" + juce::String (ch) + "_").existsAsFile());

    // Stichproben in erster und letzter Spur: bei LAUFENDER Aufnahme kürzt
    // enqueueExport den Snapshot eines vollen Rings auf Kapazität − 2×Marge
    // (Überholschutz-Vorsorge); dazu identische BWF-TimeReference
    // (ALIGNMENT: gemeinsamer Anker, kein Padding) und bitexakter Inhalt
    const auto margin = ringCapacity / conduit::CaptureWriter::overrunMarginDivisor;
    const auto expectedLength = ringCapacity - 2 * margin;

    juce::String timeRefFirst, timeRefLast;
    double rateFirst = 0.0, rateLast = 0.0;
    const auto first = readWavFile (findTrackFile ("_in1_"), timeRefFirst, rateFirst);
    const auto last  = readWavFile (findTrackFile ("_in16_"), timeRefLast, rateLast);

    // Kanal 0 wird in der Snapshot-Schleife ZUERST eingefroren → er stellt
    // den exportStart und bekommt kein Padding. Spätere Kanäle frieren ein
    // paar Blöcke später ein (der Feeder läuft währenddessen weiter) und
    // beginnen mit exakt so viel Padding-Stille — das IST das Alignment.
    REQUIRE (first.getNumSamples() == expectedLength);
    const auto padLast = last.getNumSamples() - expectedLength;
    REQUIRE (padLast >= 0);
    REQUIRE (padLast <= margin);  // Drift während der Schleife bleibt klein
    REQUIRE (juce::exactlyEqual (rateFirst, sampleRate));
    REQUIRE (juce::exactlyEqual (rateLast, sampleRate));
    REQUIRE (timeRefFirst.isNotEmpty());
    REQUIRE (timeRefFirst == timeRefLast);

    const auto exportStart = static_cast<std::uint64_t> (timeRefFirst.getLargeIntValue());
    int mismatches = 0;
    for (int i = 0; i < first.getNumSamples(); i += 997)
    {
        const auto expected = serviceRampScale
                            * rampValue (exportStart + static_cast<std::uint64_t> (i));
        if (! juce::exactlyEqual (first.getSample (0, i), expected))
            ++mismatches;
    }
    for (int i = 0; i < last.getNumSamples(); i += 997)
    {
        // Vor dem Padding-Ende exakt Stille, danach die Rampe ab exportStart
        const auto expected = i < padLast
            ? 0.0f
            : serviceRampScale * rampValue (exportStart + static_cast<std::uint64_t> (i));
        if (! juce::exactlyEqual (last.getSample (0, i), expected))
            ++mismatches;
    }
    REQUIRE (mismatches == 0);
}

//==============================================================================
TEST_CASE ("CaptureService: Device-/Samplerate-Wechsel exportiert aktives Material vorab",
           "[capture][stress]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;

    const auto exportDir = temp.folder.getChildFile ("exports");
    REQUIRE (exportDir.createDirectory());
    temp.settings->setExportDirectory (exportDir);
    temp.settings->setExportBitDepth (32);

    CaptureService service (*temp.settings);
    temp.settings->setBufferHost (&service);
    service.prepare (1000.0, 32, 2);

    juce::AudioBuffer<float> buffer (2, 32);
    std::uint64_t position = 0;
    feedServiceBlocks (service, buffer, 100, position);

    SECTION ("ohne aktive Kanäle: kein Auto-Export, Clock-Reset + Reallokation")
    {
        service.prepare (2000.0, 32, 2);

        REQUIRE_FALSE (service.isExportBusy());
        REQUIRE (exportDir.findChildFiles (juce::File::findFiles, false, "*.wav").isEmpty());
        REQUIRE (service.getSampleClock().now() == 0);
        REQUIRE (service.getRingCapacitySamples() == 15 * 60 * 2000);  // neue Rate
    }

    SECTION ("offenes Gate: Auto-Export VOR der Invalidierung (Sicherheitsnetz)")
    {
        // Aufnahme anlaufen lassen (Gate-Seam + Pool-Service wie im Timer)
        service.openGate (0);
        feedServiceBlocks (service, buffer, 1, position);
        service.runRamGuard();
        feedServiceBlocks (service, buffer, 1, position);

        const auto* oldChannel = service.getChannel (0);
        REQUIRE (oldChannel != nullptr);
        REQUIRE (oldChannel->getState() == CaptureChannel::State::recording);

        int guard = 0;
        while (! oldChannel->isTakeoverComplete() && guard++ < 200)
            feedServiceBlocks (service, buffer, 1, position);
        REQUIRE (oldChannel->isTakeoverComplete());

        // Dieser Bereich MUSS in der Datei landen — prepare friert ihn ein
        const auto frozen = oldChannel->getReadableRange();
        REQUIRE (frozen.to == position);
        REQUIRE (frozen.to > frozen.from);

        // Device-/Samplerate-Wechsel mit offenem Gate
        service.prepare (2000.0, 32, 2);
        // oldChannel zeigt in den ausgemusterten Satz — ab hier nicht mehr
        // anfassen (die Export-Pins halten ihn nur für den Writer am Leben)

        // Invalidierung wie dokumentiert: Clock-Reset, frischer Satz (idle)
        REQUIRE (service.getSampleClock().now() == 0);
        REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::idle);
        REQUIRE (service.getRingCapacitySamples() == 15 * 60 * 2000);

        // Auto-Export abwarten — Guard-Ticks räumen danach den alten Satz
        REQUIRE (waitUntil ([&] { service.runRamGuard(); return ! service.isExportBusy(); }));

        auto files = exportDir.findChildFiles (juce::File::findFiles, false, "*.wav");
        REQUIRE (files.size() == 1);

        juce::String timeRef;
        double fileRate = 0.0;
        const auto wav = readWavFile (files.getReference (0), timeRef, fileRate);

        // Exportiert mit der ALTEN Rate (mit ihr wurde aufgenommen)
        REQUIRE (juce::exactlyEqual (fileRate, 1000.0));
        REQUIRE (timeRef == juce::String (frozen.from));
        REQUIRE (wav.getNumSamples() == static_cast<int> (frozen.to - frozen.from));

        int mismatches = 0;
        for (int i = 0; i < wav.getNumSamples(); ++i)
            if (! juce::exactlyEqual (wav.getSample (0, i),
                                      serviceRampScale
                                          * rampValue (frozen.from
                                                       + static_cast<std::uint64_t> (i))))
                ++mismatches;
        REQUIRE (mismatches == 0);

        // Frischer Satz ist voll funktionsfähig: neue Aufnahme läuft an
        std::uint64_t freshPosition = 0;
        feedServiceBlocks (service, buffer, 100, freshPosition);
        service.openGate (0);
        feedServiceBlocks (service, buffer, 1, freshPosition);
        service.runRamGuard();
        feedServiceBlocks (service, buffer, 1, freshPosition);
        REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::recording);
        REQUIRE (service.getSampleClock().now() == freshPosition);
    }
}

//==============================================================================
TEST_CASE ("CaptureService-Stress: RAM-Wächter räumt nur gehaltene Kanäle, nie laufende Aufnahmen",
           "[capture][stress]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;

    // Wie der bestehende RAM-Wächter-Test: 30-min-Ringe bei 48 kHz und 1 GB
    // Limit lassen bei 4 Kanälen nur 2–3 Segmente zu (Pool-Knappheit)
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

    // Pool voll ausschöpfen: maxSegments Kanäle nehmen auf
    for (int ch = 0; ch < maxSegments; ++ch)
    {
        service.openGate (ch);
        feedServiceBlocks (service, buffer, 2, position);
        service.runRamGuard();
        feedServiceBlocks (service, buffer, 2, position);
        REQUIRE (service.getChannel (ch)->getState() == CaptureChannel::State::recording);
    }

    // Kanal 0 → held (inaktiv), die übrigen laufen weiter
    service.closeGate (0);
    feedServiceBlocks (service, buffer, 1, position);
    REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::held);

    // Aushungern: der Wächter gibt NUR den gehaltenen Kanal 0 frei —
    // die laufenden Aufnahmen bleiben unangetastet
    const int extra = maxSegments;
    service.openGate (extra);
    feedServiceBlocks (service, buffer, 1, position);
    service.runRamGuard();
    REQUIRE (service.isRamWarningActive());

    feedServiceBlocks (service, buffer, 1, position);  // Audio quittiert die Freigabe
    REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::idle);
    for (int ch = 1; ch < maxSegments; ++ch)
        REQUIRE (service.getChannel (ch)->getState() == CaptureChannel::State::recording);

    service.runRamGuard();  // Rückläufer bedient die wartende Anforderung
    feedServiceBlocks (service, buffer, 1, position);
    REQUIRE (service.getChannel (extra)->getState() == CaptureChannel::State::recording);

    // Jetzt ist KEIN Kanal mehr held — ein erneuter Anforderer hungert,
    // aber der Wächter darf laufende Aufnahmen NIEMALS freigeben
    service.openGate (0);
    feedServiceBlocks (service, buffer, 1, position);

    for (int tick = 0; tick < 5; ++tick)
    {
        service.runRamGuard();
        feedServiceBlocks (service, buffer, 2, position);
    }

    REQUIRE (service.isRamWarningActive());
    REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::awaitingSegment);
    for (int ch = 1; ch <= extra; ++ch)
        REQUIRE (service.getChannel (ch)->getState() == CaptureChannel::State::recording);
}

//==============================================================================
TEST_CASE ("CaptureChannel-Stress: Export-Leser gegen Freigabe und laufenden Audio Thread",
           "[capture][stress][threading]")
{
    constexpr int blockSize = 256;
    constexpr int window = 512;
    constexpr int cycles = 60;

    PreRollBuffer preRoll;
    BufferPool pool;
    CaptureChannel channel;
    preRoll.prepare (2048);
    pool.prepare (1 << 14, 2, 1);
    channel.prepare (preRoll, pool, 2 * blockSize);  // Budget ≥ 2× Block

    std::atomic<bool> audioDone { false };
    std::atomic<bool> wantRelease { false };
    std::atomic<int> completedCycles { 0 };
    std::atomic<int> stuckErrors { 0 };
    std::atomic<std::int64_t> successfulReads { 0 };
    std::atomic<int> dataErrors { 0 };

    // Audio Thread: Aufnahme-Zyklen open → recording → held → Freigabe
    // (die der Export-Leser über die Dekker-Barriere hinauszögern kann)
    std::thread audioThread ([&]
    {
        std::vector<float> block (static_cast<size_t> (blockSize));
        std::uint64_t position = 0;

        auto feedBlock = [&]
        {
            fillRamp (block.data(), blockSize, position);
            {
                const conduit::rt::ScopedRealtimeSection rtAudit;
                channel.process (block.data(), blockSize, position);
                preRoll.write (block.data(), blockSize, position);
            }
            position += static_cast<std::uint64_t> (blockSize);
        };

        auto feedUntil = [&] (auto&& predicate)
        {
            for (int i = 0; i < 100'000; ++i)
            {
                if (predicate())
                    return true;
                feedBlock();
            }
            return false;
        };

        for (int cycle = 0; cycle < cycles; ++cycle)
        {
            feedBlock();
            channel.openGate (position, window);

            // Pool-Brücke: der MT bedient den Pool nebenläufig
            if (! feedUntil ([&] { return channel.getState() == CaptureChannel::State::recording
                                          && channel.isTakeoverComplete(); }))
            {
                stuckErrors.fetch_add (1, std::memory_order_relaxed);
                break;
            }

            for (int i = 0; i < 8; ++i)
                feedBlock();

            channel.closeGate (position);

            // Freigabe anfordern (läuft auf dem MT); bei aktivem Leser wird
            // sie aufgeschoben — irgendwann quittiert Audio auf idle
            wantRelease.store (true, std::memory_order_release);
            if (! feedUntil ([&] { return channel.getState() == CaptureChannel::State::idle; }))
            {
                stuckErrors.fetch_add (1, std::memory_order_relaxed);
                break;
            }

            completedCycles.fetch_add (1, std::memory_order_relaxed);
        }

        audioDone.store (true, std::memory_order_release);
    });

    // "Writer-Thread": Halte-Protokoll im Dauerfeuer — gelesen wird NUR
    // zwischen tryBeginExportRead/endExportRead, Inhalte gegen die Rampe.
    // read() validiert nach dem Kopieren: true ⇒ Inhalt garantiert korrekt.
    std::thread readerThread ([&]
    {
        std::vector<float> samples (64);

        while (! audioDone.load (std::memory_order_acquire))
        {
            if (! channel.tryBeginExportRead())
            {
                std::this_thread::yield();  // Barriere steht — Freigabe läuft
                continue;
            }

            const auto range = channel.getReadableRange();
            if (range.to >= range.from + 64)
            {
                const auto from = range.to - 64;  // dicht hinter dem Schreib-Cursor
                if (channel.read (from, samples.data(), 64))
                {
                    successfulReads.fetch_add (1, std::memory_order_relaxed);
                    for (int i = 0; i < 64; ++i)
                        if (! juce::exactlyEqual (samples[static_cast<size_t> (i)],
                                                  rampValue (from + static_cast<std::uint64_t> (i))))
                            dataErrors.fetch_add (1, std::memory_order_relaxed);
                }
            }

            channel.endExportRead();
            std::this_thread::yield();
        }
    });

    // [MT] Pool-Service + Freigaben — das Muster des RAM-Wächter-Timers
    while (! audioDone.load (std::memory_order_acquire))
    {
        pool.service();
        if (wantRelease.exchange (false, std::memory_order_acq_rel))
            channel.requestRelease();
        std::this_thread::yield();
    }
    pool.service();

    audioThread.join();
    readerThread.join();

    REQUIRE (stuckErrors.load() == 0);
    REQUIRE (completedCycles.load() == cycles);
    REQUIRE (dataErrors.load() == 0);          // validierte Reads sind exakt
    CHECK (successfulReads.load() > 0);        // der Leser kam real zum Zug
    REQUIRE (channel.getState() == CaptureChannel::State::idle);
}
