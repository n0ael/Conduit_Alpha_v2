#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

#include "Core/Capture/CaptureService.h"

using Catch::Approx;
using conduit::CaptureChannel;
using conduit::CaptureGate;
using conduit::CaptureService;
using conduit::CaptureSettings;

namespace
{

/** dB → linearer RMS-Pegel für synthetische Pegelverläufe. */
float level (float db)
{
    return juce::Decibels::decibelsToGain (db);
}

/** Settings-Persistenz in ein Temp-Verzeichnis statt in die echte
    Conduit.settings des Users — Verzeichnis wird im Dtor gelöscht. */
struct TempCaptureSettings
{
    TempCaptureSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitCaptureGateTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();

        juce::PropertiesFile::Options options;
        options.applicationName = "ConduitCaptureGateTests";
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

/** Füllt alle Kanäle mit einem konstanten Pegel (RMS == Pegel, exakt ab dem
    ersten Block dank Warm-Start des Meters) und füttert den Tap. */
void feedConstantBlocks (CaptureService& service, juce::AudioBuffer<float>& buffer,
                         float constantLevel, int count)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] = constantLevel;
    }

    for (int i = 0; i < count; ++i)
        service.processInputTap (buffer, buffer.getNumChannels());
}

} // namespace

//==============================================================================
TEST_CASE ("CaptureGate: Zustandsmaschine mit Hysterese und Hold in Samples", "[capture]")
{
    CaptureGate gate;
    gate.prepare (-40.0f);

    constexpr std::int64_t hold = 1000;  // Samples, nicht Wall-Clock
    constexpr int block = 100;

    REQUIRE (gate.getStatus() == CaptureGate::Status::idle);

    // Unter der Schwelle: bleibt zu
    REQUIRE (gate.process (level (-50.0f), block, hold) == CaptureGate::Event::none);

    // Im Hysterese-Band (zwischen Close −46 und Open −40): öffnet NICHT
    REQUIRE (gate.process (level (-43.0f), block, hold) == CaptureGate::Event::none);
    REQUIRE (gate.getStatus() == CaptureGate::Status::idle);

    // Über der Schwelle: öffnet genau einmal
    REQUIRE (gate.process (level (-35.0f), block, hold) == CaptureGate::Event::opened);
    REQUIRE (gate.getStatus() == CaptureGate::Status::recording);
    REQUIRE (gate.process (level (-35.0f), block, hold) == CaptureGate::Event::none);

    SECTION ("Hold läuft in Samples ab und schließt erst nach holdSamples")
    {
        // 9 Blöcke à 100 unter Close-Schwelle: 900 < 1000 — noch offen
        for (int i = 0; i < 9; ++i)
            REQUIRE (gate.process (level (-60.0f), block, hold) == CaptureGate::Event::none);
        REQUIRE (gate.getStatus() == CaptureGate::Status::recording);

        // 10. Block erreicht 1000 → schließt, Material gilt als gehalten
        REQUIRE (gate.process (level (-60.0f), block, hold) == CaptureGate::Event::closed);
        REQUIRE (gate.getStatus() == CaptureGate::Status::held);
    }

    SECTION ("Flattern an der Schwelle: die Hysterese hält das Gate offen")
    {
        // RMS pendelt zwischen −44 und −38 — beides über Close (−46):
        // der Hold-Zähler startet nie, auch nach 10 000 Samples nicht
        for (int i = 0; i < 50; ++i)
        {
            REQUIRE (gate.process (level (-44.0f), block, hold) == CaptureGate::Event::none);
            REQUIRE (gate.process (level (-38.0f), block, hold) == CaptureGate::Event::none);
        }
        REQUIRE (gate.getStatus() == CaptureGate::Status::recording);
    }

    SECTION ("Hold-Reset: ein Signal-Burst startet die Hold-Zeit neu")
    {
        for (int i = 0; i < 9; ++i)
            gate.process (level (-60.0f), block, hold);  // 900 von 1000

        gate.process (level (-35.0f), block, hold);      // Burst: Zähler zurück

        for (int i = 0; i < 9; ++i)
            REQUIRE (gate.process (level (-60.0f), block, hold) == CaptureGate::Event::none);
        REQUIRE (gate.getStatus() == CaptureGate::Status::recording);

        REQUIRE (gate.process (level (-60.0f), block, hold) == CaptureGate::Event::closed);
    }

    SECTION ("Wiedereröffnung aus held und Freigabe-Quittung")
    {
        for (int i = 0; i < 10; ++i)
            gate.process (level (-60.0f), block, hold);
        REQUIRE (gate.getStatus() == CaptureGate::Status::held);

        // Neues Signal: held → recording
        REQUIRE (gate.process (level (-30.0f), block, hold) == CaptureGate::Event::opened);
        REQUIRE (gate.getStatus() == CaptureGate::Status::recording);

        // Quittung bei OFFENEM Gate ist ein No-op
        gate.notifyContentDiscarded();
        REQUIRE (gate.getStatus() == CaptureGate::Status::recording);

        // Wieder held → Storage freigegeben → idle
        for (int i = 0; i < 10; ++i)
            gate.process (level (-60.0f), block, hold);
        REQUIRE (gate.getStatus() == CaptureGate::Status::held);

        gate.notifyContentDiscarded();
        REQUIRE (gate.getStatus() == CaptureGate::Status::idle);
    }

    SECTION ("Kalibrierung verschiebt die Schwelle zur Laufzeit")
    {
        // AutoCalibrator hebt auf −20: der bisherige Pegel −35 liegt jetzt
        // unter Close (−26) — die Hold-Zeit beginnt zu laufen
        gate.setEffectiveThresholdDb (-20.0f);
        REQUIRE (gate.getEffectiveThresholdDb() == Approx (-20.0f));

        auto lastEvent = CaptureGate::Event::none;
        for (int i = 0; i < 10; ++i)
            lastEvent = gate.process (level (-35.0f), block, hold);

        REQUIRE (lastEvent == CaptureGate::Event::closed);
        REQUIRE (gate.getStatus() == CaptureGate::Status::held);
    }

    SECTION ("reset() schließt sofort und verwirft den Status")
    {
        gate.reset();
        REQUIRE (gate.getStatus() == CaptureGate::Status::idle);
    }
}

//==============================================================================
TEST_CASE ("CaptureService: pure Helfer — Hold-Samples und effektive Schwelle", "[capture]")
{
    // Hold in Samples: holdMinutes × 60 × sampleRate, int64 trägt auch 192 kHz
    REQUIRE (CaptureService::computeHoldSamples (10, 48000.0) == 28'800'000LL);
    REQUIRE (CaptureService::computeHoldSamples (30, 192000.0) == 345'600'000LL);
    REQUIRE (CaptureService::computeHoldSamples (0, 48000.0) == 0);
    REQUIRE (CaptureService::computeHoldSamples (10, 0.0) == 0);

    // Ohne Auto-Kalibrierung: der manuelle Threshold direkt
    REQUIRE (CaptureService::computeEffectiveThresholdDb (-40.0f, level (-30.0f), false)
             == Approx (-40.0f));

    // Floor −30 dB + 12 dB Headroom liegt über dem manuellen Wert
    REQUIRE (CaptureService::computeEffectiveThresholdDb (-40.0f, level (-30.0f), true)
             == Approx (-18.0f).margin (0.01));

    // Leiser Floor: der manuelle Threshold ist die Override-Untergrenze
    REQUIRE (CaptureService::computeEffectiveThresholdDb (-40.0f, level (-70.0f), true)
             == Approx (-40.0f));

    // Stille (Floor 0 linear) fällt definiert auf den manuellen Wert zurück
    REQUIRE (CaptureService::computeEffectiveThresholdDb (-40.0f, 0.0f, true)
             == Approx (-40.0f));
}

//==============================================================================
TEST_CASE ("CaptureService: Auto-Kalibrierung hebt die Schwelle über Dauerbrummen", "[capture]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;
    CaptureService service (*temp.settings);
    service.prepare (48000.0, 480, 1);

    juce::AudioBuffer<float> buffer (1, 480);

    // Dauerbrummen bei −45 dB: unter dem manuellen Threshold (−40),
    // das Gate bleibt zu — aber der Noise-Floor lernt den Pegel
    feedConstantBlocks (service, buffer, level (-45.0f), 20);
    REQUIRE (service.getGate (0) != nullptr);
    REQUIRE (service.getGate (0)->getStatus() == CaptureGate::Status::idle);

    // 1-Hz-Tick publiziert: max(−40, −45 + 12) = −33
    service.runAutoCalibration();
    REQUIRE (service.getGate (0)->getEffectiveThresholdDb() == Approx (-33.0f).margin (0.1));

    // −38 dB hätte den manuellen Threshold gerissen — die kalibrierte
    // Schwelle liegt über dem Brummen, das Gate bleibt zu
    feedConstantBlocks (service, buffer, level (-38.0f), 50);
    REQUIRE (service.getGate (0)->getStatus() == CaptureGate::Status::idle);
    REQUIRE_FALSE (service.isAnyChannelActive());

    // −20 dB liegt klar über der kalibrierten Schwelle → öffnet
    feedConstantBlocks (service, buffer, level (-20.0f), 10);
    REQUIRE (service.getGate (0)->getStatus() == CaptureGate::Status::recording);
    REQUIRE (service.isAnyChannelActive());

    // Auto-Kalibrierung aus: der manuelle Threshold gilt wieder direkt
    temp.settings->setAutoCalibrate (false);
    service.runAutoCalibration();
    REQUIRE (service.getGate (0)->getEffectiveThresholdDb() == Approx (-40.0f));
}

//==============================================================================
TEST_CASE ("CaptureService: Gate-Detektion steuert die Aufnahme end-to-end", "[capture]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;
    temp.settings->setHoldMinutes (1);  // Hold = 60 000 Samples bei 1 kHz

    CaptureService service (*temp.settings);
    service.prepare (1000.0, 32, 1);  // niedrige Samplerate hält den Test klein

    juce::AudioBuffer<float> buffer (1, 32);

    // Stille: nichts passiert
    feedConstantBlocks (service, buffer, 0.0f, 10);
    REQUIRE (service.getGate (0)->getStatus() == CaptureGate::Status::idle);
    REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::idle);

    // Signal −20 dB über Schwelle −40: Gate öffnet, Kanal fordert Segment an
    feedConstantBlocks (service, buffer, level (-20.0f), 5);
    REQUIRE (service.getGate (0)->getStatus() == CaptureGate::Status::recording);
    REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::awaitingSegment);

    service.runRamGuard();  // Pool publiziert das vorgehaltene Segment
    feedConstantBlocks (service, buffer, level (-20.0f), 5);
    REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::recording);

    // Stille: der RMS braucht ein paar Blöcke unter Close (−46), dann zählt
    // der Hold — 900 Blöcke ≈ 28 800 Samples sind klar unter 60 000
    feedConstantBlocks (service, buffer, 0.0f, 900);
    REQUIRE (service.getGate (0)->getStatus() == CaptureGate::Status::recording);
    REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::recording);

    // Weitere 1100 Blöcke: Hold-Zeit überschritten → Gate zu, Material held
    feedConstantBlocks (service, buffer, 0.0f, 1100);
    REQUIRE (service.getGate (0)->getStatus() == CaptureGate::Status::held);
    REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::held);
    REQUIRE (service.isAnyChannelActive());  // held zählt als aktiv (Resize-Policy)

    // Neues Signal: Wiedereröffnung aus held
    feedConstantBlocks (service, buffer, level (-20.0f), 5);
    REQUIRE (service.getGate (0)->getStatus() == CaptureGate::Status::recording);
    REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::recording);

    // RMS erst abklingen lassen (Hold hält das Gate offen): liegt beim
    // Invalidate noch Signal an, öffnet die Detektion sofort wieder —
    // gewollt, aber hier soll der Ruhe-Pfad enden
    feedConstantBlocks (service, buffer, 0.0f, 50);
    REQUIRE (service.getGate (0)->getStatus() == CaptureGate::Status::recording);

    // Invalidate (Resize bestätigt): Gates zu, Detektion zurückgesetzt
    service.invalidateAllBuffers();
    feedConstantBlocks (service, buffer, 0.0f, 1);
    REQUIRE (service.getGate (0)->getStatus() == CaptureGate::Status::idle);
    REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::idle);
    REQUIRE_FALSE (service.isAnyChannelActive());
}
