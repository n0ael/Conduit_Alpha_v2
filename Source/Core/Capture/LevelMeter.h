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
      - RMS über ein einstellbares Fenster (One-Pole auf dem Signalquadrat,
        Default ~360 ms) — der stetige, hellere Balken.
      - Peak mit sofortigem Attack und einstellbarem Release (Default
        ~0,45 s) — der halbhelle Balken unter dem RMS.
      - Peak-Hold: hält das Maximum (einstellbar, Default ~1,6 s), dann
        Abfall (dünner Marker).
      - Clip-Latch: gesetzt bei |x| >= 1.0 (0 dBFS), bleibt bis resetClip().

    Ballistik ist KLASSENWEIT einstellbar (User-Feintuning 14.07.2026,
    Metering-Tab): setGlobalBallistics() speist statische Atomics, die ALLE
    Instanzen (Input/Output, FX-Chassis, Looper-Tracks) pro Block relaxed
    lesen — persönlicher Geschmack gilt app-weit, kein Verdrahten jeder
    einzelnen Meter-Instanz nötig.

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

    /** [Audio Thread] Misst aus rohen Kanal-Pointern (Looper-Busse, kein
        AudioBuffer nötig). channelData[i] == nullptr ⇒ Stille: die
        Ballistik fällt normal ab, kein neuer Clip-Latch. numChannels über
        activeChannels hinaus werden ignoriert. */
    void processPointers (const float* const* channelData, int numChannels,
                          int numSamples) noexcept;

    //==========================================================================
    // [beliebiger Thread] Außerhalb des aktiven Kanalbereichs: 0.0f / false
    [[nodiscard]] float getPeak (int channel) const noexcept;
    [[nodiscard]] float getPeakHold (int channel) const noexcept;
    [[nodiscard]] float getRms (int channel) const noexcept;
    [[nodiscard]] bool  isClipped (int channel) const noexcept;

    /** [beliebiger Thread] Clip-Latch eines Kanals löschen (UI-Klick / Auto). */
    void resetClip (int channel) noexcept;

    /** [Message Thread] Auto-Clear des Clip-Latch: 0 = nie (nur manueller
        Reset), > 0 = Latch verlischt nach so vielen Sekunden von selbst.
        Vom EngineProcessor aus den MeterSettings gespeist. */
    void setClipHoldSeconds (float seconds) noexcept;

    [[nodiscard]] int getNumActiveChannels() const noexcept { return activeChannels; }

    //==========================================================================
    // Klassenweite Ballistik (Metering-Tab, MeterSettings → EngineProcessor).
    // Setter [Message Thread], Reads [Audio Thread, relaxed pro Block].

    // User-Defaults 14.07.2026 (im Feldtest ertastet)
    static constexpr float defaultRmsWindowSeconds   = 0.36f;
    static constexpr float defaultPeakReleaseSeconds = 0.45f;
    static constexpr float defaultPeakHoldSeconds    = 1.6f;

    static void setGlobalBallistics (float rmsWindowSec, float peakReleaseSec,
                                     float peakHoldSec) noexcept
    {
        rmsWindowStore().store (juce::jlimit (0.01f, 5.0f, rmsWindowSec),
                                std::memory_order_relaxed);
        peakReleaseStore().store (juce::jlimit (0.05f, 10.0f, peakReleaseSec),
                                  std::memory_order_relaxed);
        peakHoldStore().store (juce::jlimit (0.1f, 10.0f, peakHoldSec),
                               std::memory_order_relaxed);
    }

    [[nodiscard]] static float getGlobalRmsWindowSeconds() noexcept
    {
        return rmsWindowStore().load (std::memory_order_relaxed);
    }
    [[nodiscard]] static float getGlobalPeakReleaseSeconds() noexcept
    {
        return peakReleaseStore().load (std::memory_order_relaxed);
    }
    [[nodiscard]] static float getGlobalPeakHoldSeconds() noexcept
    {
        return peakHoldStore().load (std::memory_order_relaxed);
    }

    //==========================================================================
    static constexpr float clipThreshold = 1.0f;  // 0 dBFS

private:
    static_assert (std::atomic<float>::is_always_lock_free
                       && std::atomic<bool>::is_always_lock_free,
                   "LevelMeter-Atomics müssen lock-free sein (Audio Thread)");

    static constexpr double peakHoldReleaseSeconds = 1.0;  // Abfall nach dem Halten

    // Funktionslokale Statics (kein Init-Order-Risiko) fuer die
    // klassenweite Ballistik — Defaults s. o.
    static std::atomic<float>& rmsWindowStore() noexcept
    {
        static std::atomic<float> value { defaultRmsWindowSeconds };
        return value;
    }
    static std::atomic<float>& peakReleaseStore() noexcept
    {
        static std::atomic<float> value { defaultPeakReleaseSeconds };
        return value;
    }
    static std::atomic<float>& peakHoldStore() noexcept
    {
        static std::atomic<float> value { defaultPeakHoldSeconds };
        return value;
    }

    void meterOneChannel (int channel, const float* data, int numSamples,
                          float rmsCoeffBlock, float peakDecay, float holdDecay,
                          double peakHoldSecondsNow, double blockSeconds) noexcept;

    /** Stille-Zweig (nullptr-Kanal in processPointers): Ballistik-Abfall
        ohne Sample-Schleife. */
    void meterSilentChannel (int channel, int numSamples, float rmsCoeffBlock,
                             float peakDecay, float holdDecay,
                             double blockSeconds) noexcept;

    double sampleRate = 48000.0;
    int activeChannels = 0;       // nur in prepare() geschrieben (Audio steht)

    // Interner Zustand — nur der Audio Thread liest/schreibt
    std::array<float, MAX_CAPTURE_CHANNELS> meanSquareState {};
    std::array<float, MAX_CAPTURE_CHANNELS> peakState {};
    std::array<float, MAX_CAPTURE_CHANNELS> peakHoldState {};
    std::array<double, MAX_CAPTURE_CHANNELS> holdRemaining {};  // Sekunden bis Abfall
    std::array<double, MAX_CAPTURE_CHANNELS> clipAgeSeconds {}; // seit Latch (Auto-Clear)
    std::array<bool, MAX_CAPTURE_CHANNELS> primed {};           // RMS-Warm-Start pro Kanal

    // Auto-Clear-Dauer des Clip-Latch [Message → Audio]; 0 = nur manuell
    std::atomic<float> clipHoldSeconds { 0.0f };

    // Publizierte Werte [Audio → UI] — unabhängige Einzelwerte, relaxed genügt
    std::array<std::atomic<float>, MAX_CAPTURE_CHANNELS> peakLevel {};
    std::array<std::atomic<float>, MAX_CAPTURE_CHANNELS> peakHoldLevel {};
    std::array<std::atomic<float>, MAX_CAPTURE_CHANNELS> rmsLevel {};
    std::array<std::atomic<bool>,  MAX_CAPTURE_CHANNELS> clipped {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};

} // namespace conduit
