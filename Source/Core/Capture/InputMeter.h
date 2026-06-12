#pragma once

#include <array>
#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>

namespace conduit
{

//==============================================================================
/** Compile-time-Obergrenze der Capture-Kanäle — fixe Arrays, kein Heap im
    RT-Pfad. 64 deckt die größten DC-coupled Interfaces (ES-3/ESX-Ketten) ab. */
inline constexpr int MAX_CAPTURE_CHANNELS = 64;

//==============================================================================
/**
    Input-Metering für das Capture-System.

    Pro Kanal: Peak (mit Release-Ballistik), RMS über ein ~50-ms-Fenster
    (One-Pole auf dem Signalquadrat) und ein Noise-Floor-Schätzer per
    Minimum-Tracking: abwärts folgt der Floor dem RMS sofort, aufwärts nur
    mit ~30-s-Release — so "vergisst" er alte Minima langsam und folgt dem
    echten Grundrauschen, ohne von Signalpassagen hochgezogen zu werden.

    Threading: prepare() auf dem Message Thread (Audio steht),
    process() auf dem Audio Thread — allocation-free, lock-free.
    Die Getter lesen std::atomic<float> von beliebigen Threads.
*/
class InputMeter
{
public:
    InputMeter() = default;

    /** [Message Thread, aus prepareToPlay] Setzt alle Messwerte zurück. */
    void prepare (double sampleRate, int numChannels);

    /** [Audio Thread] Misst die ersten numChannels Kanäle des Buffers. */
    void process (const juce::AudioBuffer<float>& buffer, int numChannels) noexcept;

    /** [Audio Thread] Misst einen einzelnen Kanal — gleiche Ballistik wie
        process(). Für virtuelle Capture-Kanäle (Taps), deren Daten aus dem
        Graph statt aus dem Input-Buffer kommen (CaptureService). No-op
        außerhalb des aktiven Kanalbereichs. */
    void processChannel (int channel, const float* data, int numSamples) noexcept;

    //==========================================================================
    // [beliebiger Thread] Außerhalb des aktiven Kanalbereichs: 0.0f
    [[nodiscard]] float getPeak (int channel) const noexcept;
    [[nodiscard]] float getRms (int channel) const noexcept;
    [[nodiscard]] float getNoiseFloor (int channel) const noexcept;

    [[nodiscard]] int getNumActiveChannels() const noexcept { return activeChannels; }

private:
    static_assert (std::atomic<float>::is_always_lock_free,
                   "InputMeter-Atomics müssen lock-free sein (Audio Thread)");

    static constexpr double rmsWindowSeconds       = 0.05;  // ~50-ms-RMS-Fenster
    static constexpr double peakReleaseSeconds     = 0.5;   // Peak-Ballistik (UI-Anzeige)
    static constexpr double noiseFloorRiseSeconds  = 30.0;  // sanfter Floor-Anstieg

    /** Gemeinsamer Mess-Kern: ein Kanal, ein Block. Warm-Start pro Kanal
        über den Floor-Sentinel (erster Block seedet den RMS-Zustand). */
    void meterOneChannel (int channel, const float* data, int numSamples,
                          float peakDecay, float floorRise) noexcept;

    double sampleRate = 48000.0;
    int activeChannels = 0;       // nur in prepare() geschrieben (Audio steht)
    float rmsCoeff = 0.0f;        // One-Pole-Koeffizient pro Sample

    // Interner Zustand — nur der Audio Thread liest/schreibt
    std::array<float, MAX_CAPTURE_CHANNELS> meanSquareState {};
    std::array<float, MAX_CAPTURE_CHANNELS> peakState {};
    std::array<float, MAX_CAPTURE_CHANNELS> floorState {};

    // Publizierte Werte [Audio → UI/Message Thread] — unabhängige Einzelwerte,
    // relaxed genügt (kein abhängiger Speicher wie bei der SampleClock)
    std::array<std::atomic<float>, MAX_CAPTURE_CHANNELS> peakLevel {};
    std::array<std::atomic<float>, MAX_CAPTURE_CHANNELS> rmsLevel {};
    std::array<std::atomic<float>, MAX_CAPTURE_CHANNELS> noiseFloor {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputMeter)
};

} // namespace conduit
