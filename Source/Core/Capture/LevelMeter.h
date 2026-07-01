#pragma once

#include <array>
#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>

#include "InputMeter.h"  // MAX_CAPTURE_CHANNELS

namespace conduit
{

//==============================================================================
/**
    Sicht-Metering im Ableton-Stil für die audio_in/audio_out-Kacheln — für
    Ein- UND Ausgänge (zwei Instanzen im EngineProcessor). Getrennt vom
    capture-spezifischen InputMeter (Noise-Floor); hier zählt der visuelle
    Pegel.

    Pro Kanal:
      - RMS über ein ~150-ms-Fenster (One-Pole auf dem Signalquadrat) — der
        stetige, hellere Balken.
      - Peak mit sofortigem Attack und ~1,5-s-Release-Ballistik.
      - Peak-Hold: hält das Maximum ~1,5 s, dann Abfall (Peak-Marker).
      - Clip-Latch: gesetzt bei |x| >= 1.0 (0 dBFS), bleibt bis resetClip().

    Threading: prepare() auf dem Message Thread (Audio steht), process() auf
    dem Audio Thread — allocation-free, lock-free. Getter/resetClip von
    beliebigen Threads (std::atomic).
*/
class LevelMeter
{
public:
    LevelMeter() = default;

    /** [Message Thread, aus prepareToPlay] Setzt alle Messwerte zurück. */
    void prepare (double sampleRate, int numChannels);

    /** [Audio Thread] Misst die ersten numChannels Kanäle des Buffers. */
    void process (const juce::AudioBuffer<float>& buffer, int numChannels) noexcept;

    //==========================================================================
    // [beliebiger Thread] Außerhalb des aktiven Kanalbereichs: 0.0f / false
    [[nodiscard]] float getPeak (int channel) const noexcept;
    [[nodiscard]] float getPeakHold (int channel) const noexcept;
    [[nodiscard]] float getRms (int channel) const noexcept;
    [[nodiscard]] bool  isClipped (int channel) const noexcept;

    /** [beliebiger Thread] Clip-Latch eines Kanals löschen (UI-Klick / Auto). */
    void resetClip (int channel) noexcept;

    [[nodiscard]] int getNumActiveChannels() const noexcept { return activeChannels; }

    //==========================================================================
    static constexpr float clipThreshold = 1.0f;  // 0 dBFS

private:
    static_assert (std::atomic<float>::is_always_lock_free
                       && std::atomic<bool>::is_always_lock_free,
                   "LevelMeter-Atomics müssen lock-free sein (Audio Thread)");

    static constexpr double rmsWindowSeconds        = 0.15;  // ~150-ms-RMS-Fenster
    static constexpr double peakReleaseSeconds       = 1.5;  // Peak-Ballistik
    static constexpr double peakHoldSeconds          = 1.5;  // Peak-Marker-Haltezeit
    static constexpr double peakHoldReleaseSeconds    = 1.0;  // Abfall nach dem Halten

    void meterOneChannel (int channel, const float* data, int numSamples,
                          float peakDecay, float holdDecay, double blockSeconds) noexcept;

    double sampleRate = 48000.0;
    int activeChannels = 0;       // nur in prepare() geschrieben (Audio steht)
    float rmsCoeff = 0.0f;        // One-Pole-Koeffizient pro Sample

    // Interner Zustand — nur der Audio Thread liest/schreibt
    std::array<float, MAX_CAPTURE_CHANNELS> meanSquareState {};
    std::array<float, MAX_CAPTURE_CHANNELS> peakState {};
    std::array<float, MAX_CAPTURE_CHANNELS> peakHoldState {};
    std::array<double, MAX_CAPTURE_CHANNELS> holdRemaining {};  // Sekunden bis Abfall
    std::array<bool, MAX_CAPTURE_CHANNELS> primed {};           // RMS-Warm-Start pro Kanal

    // Publizierte Werte [Audio → UI] — unabhängige Einzelwerte, relaxed genügt
    std::array<std::atomic<float>, MAX_CAPTURE_CHANNELS> peakLevel {};
    std::array<std::atomic<float>, MAX_CAPTURE_CHANNELS> peakHoldLevel {};
    std::array<std::atomic<float>, MAX_CAPTURE_CHANNELS> rmsLevel {};
    std::array<std::atomic<bool>,  MAX_CAPTURE_CHANNELS> clipped {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};

} // namespace conduit
