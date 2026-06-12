#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

#include <juce_audio_basics/juce_audio_basics.h>

namespace conduit
{

//==============================================================================
/**
    Signal-Detektion eines Capture-Kanals — entscheidet im Input-Tap, wann
    das Aufnahme-Gate öffnet und schließt (Capture-Baustein 4).

    Zustandsmaschine: IDLE → OPEN → (Hold abgelaufen) → IDLE.
      - Öffnen, sobald der Block-RMS die effektive Schwelle überschreitet.
      - Schließen erst, wenn der RMS durchgehend für die Hold-Zeit unter
        Schwelle − 6 dB bleibt (Hysterese) — jeder Ausreißer über die
        Close-Schwelle setzt den Hold-Zähler zurück, Flattern an der
        Schwelle hält das Gate offen. Hold zählt in SAMPLES
        (holdMinutes × 60 × sampleRate, siehe
        CaptureService::computeHoldSamples), nie Wall-Clock —
        Sample-Positionen sind die einzige Zeitbasis des Capture-Systems.

    Der publizierte Status (idle/recording/held) ist die Außensicht für die
    UI: recording solange das Gate offen ist, held nach dem Schließen — der
    Pufferinhalt bleibt gebunden, bis Export oder RAM-Wächter ihn freigeben;
    der CaptureService quittiert die Freigabe über notifyContentDiscarded().

    Die effektive Schwelle [atomic, Message → Audio] publiziert der
    AutoCalibrator des CaptureService (1-Hz-Tick): bei autoCalibrate
    max(Settings-Threshold, NoiseFloor + 12 dB), sonst der Settings-
    Threshold direkt. Die dB→Gain-Umrechnung wird audio-seitig gecacht und
    nur bei geänderter Schwelle neu berechnet — kein pow pro Block.

    Threading: process/reset/notifyContentDiscarded NUR Audio Thread,
    prepare() auf dem Message Thread bei stehendem Audio.
    setEffectiveThresholdDb vom Message Thread; die Getter sind atomare
    Momentaufnahmen von jedem Thread.
*/
class CaptureGate
{
public:
    enum class Status : int { idle, recording, held };
    enum class Event  : int { none, opened, closed };

    static constexpr float hysteresisDb = 6.0f;

    CaptureGate() = default;

    /** [Message Thread, Audio steht] Detektion zurücksetzen, Schwelle setzen. */
    void prepare (float initialThresholdDb) noexcept
    {
        effectiveThresholdDb.store (initialThresholdDb, std::memory_order_relaxed);
        cachedThresholdDb = unsetThreshold;
        gateOpen = false;
        samplesBelowClose = 0;
        status.store (Status::idle, std::memory_order_relaxed);
    }

    //==========================================================================
    /** [Audio Thread] Block-RMS gegen die effektive Schwelle. Liefert das
        Übergangs-Ereignis dieses Blocks — der Aufrufer verdrahtet
        opened/closed mit der Gate-API des CaptureService. */
    Event process (float rmsLinear, int numSamples, std::int64_t holdSamples) noexcept
    {
        if (numSamples <= 0)
            return Event::none;

        updateThresholdGains();

        if (! gateOpen)
        {
            if (rmsLinear > openGain)
            {
                gateOpen = true;
                samplesBelowClose = 0;
                status.store (Status::recording, std::memory_order_release);
                return Event::opened;
            }

            return Event::none;
        }

        if (rmsLinear < closeGain)
        {
            samplesBelowClose += numSamples;
            if (samplesBelowClose >= holdSamples)
            {
                gateOpen = false;
                samplesBelowClose = 0;
                status.store (Status::held, std::memory_order_release);
                return Event::closed;
            }
        }
        else
        {
            samplesBelowClose = 0;  // Hysterese-Band oder Signal: Hold neu starten
        }

        return Event::none;
    }

    /** [Audio Thread] Invalidate / Puffersatz-Swap: Gate zu, Status idle. */
    void reset() noexcept
    {
        gateOpen = false;
        samplesBelowClose = 0;
        status.store (Status::idle, std::memory_order_release);
    }

    /** [Audio Thread] Gehaltenes Material wurde freigegeben (RAM-Wächter,
        Invalidate, später Export) — der UI-Status folgt von held auf idle.
        No-op bei offenem Gate. */
    void notifyContentDiscarded() noexcept
    {
        if (! gateOpen)
            status.store (Status::idle, std::memory_order_release);
    }

    //==========================================================================
    /** [Message Thread] AutoCalibrator publiziert die effektive Schwelle. */
    void setEffectiveThresholdDb (float db) noexcept
    {
        effectiveThresholdDb.store (db, std::memory_order_relaxed);
    }

    //==========================================================================
    // Status [beliebiger Thread] — atomare Momentaufnahmen

    [[nodiscard]] Status getStatus() const noexcept
    {
        return status.load (std::memory_order_acquire);
    }

    [[nodiscard]] float getEffectiveThresholdDb() const noexcept
    {
        return effectiveThresholdDb.load (std::memory_order_relaxed);
    }

private:
    void updateThresholdGains() noexcept
    {
        const auto db = effectiveThresholdDb.load (std::memory_order_relaxed);
        if (! juce::exactlyEqual (db, cachedThresholdDb))
        {
            cachedThresholdDb = db;
            openGain  = juce::Decibels::decibelsToGain (db);
            closeGain = juce::Decibels::decibelsToGain (db - hysteresisDb);
        }
    }

    static_assert (std::atomic<float>::is_always_lock_free
                       && std::atomic<Status>::is_always_lock_free,
                   "CaptureGate-Atomics müssen lock-free sein (Audio Thread)");

    // Sentinel: erzwingt die Gain-Berechnung im ersten process()-Aufruf
    static constexpr float unsetThreshold = std::numeric_limits<float>::max();

    // Nur Audio Thread (Ausnahme: prepare(), Audio steht)
    bool gateOpen = false;
    std::int64_t samplesBelowClose = 0;
    float cachedThresholdDb = unsetThreshold;
    float openGain  = 0.0f;
    float closeGain = 0.0f;

    // Publiziert
    std::atomic<float>  effectiveThresholdDb { 0.0f };  // [Message → Audio]
    std::atomic<Status> status { Status::idle };        // [Audio → UI]

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureGate)
};

} // namespace conduit
