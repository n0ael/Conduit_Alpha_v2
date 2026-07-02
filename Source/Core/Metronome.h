#pragma once

#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Interfaces/IClockSource.h"

namespace conduit
{

//==============================================================================
/**
    Link-synchrones Metronom (Push-Header, User-Wunsch 2026-07-02) —
    allocation- und lock-frei (CLAUDE.md 3.1).

    Läuft im EngineProcessor NACH dem GraphFader (auf das finale Signal
    addiert, damit Graph-Swaps den Click nicht mitfaden) und VOR dem
    Sicht-Metering Out; der Capture-Input-Tap am Blockanfang bleibt sauber.

    Click: kurzer Sinus-Burst mit Exponential-Decay (~20 ms), sample-genau
    an den Beat-Grenzen des ClockState (floor-Überquerung wie das
    Launch-Quantisierungs-Muster 4.5). Downbeat (Beat % 4 == 0, Quantum der
    LinkClock) klingt eine Oktave höher. Der Click läuft, solange das
    Metronom an ist — Conduit ist frei laufend, der Session-Beat zählt auch
    ohne Transport (bewusst KEIN isPlaying-Gate).

    Ziel-Kanäle: ein Stereo-Anker (Kanalpaar 2n/2n+1, z. B. Headphones) aus
    den TransportSettings. Deaktivieren stoppt nur neue Trigger — der
    laufende Click klingt aus (kein Knacks).

    Threading: prepare()/Setter → Message Thread (Atomics), process() →
    Audio Thread.
*/
class Metronome final
{
public:
    Metronome() = default;

    /** Vor Audio-Start (EngineProcessor::prepareToPlay). */
    void prepare (double sampleRate) noexcept;

    // Message Thread (applyTransportSettings)
    void setEnabled (bool shouldBeEnabled) noexcept
    { enabled.store (shouldBeEnabled, std::memory_order_relaxed); }

    /** Stereo-Anker: Click auf Kanäle anchor*2 und anchor*2+1. */
    void setAnchor (int pairIndex) noexcept
    { anchor.store (juce::jmax (0, pairIndex), std::memory_order_relaxed); }

    /** Audio Thread — addiert den Click auf die Anker-Kanäle. */
    void process (juce::AudioBuffer<float>& buffer, int numOutputChannels,
                  const ClockState& clock) noexcept;

private:
    static constexpr float clickGain     = 0.4f;
    static constexpr double beatFreqHz    = 880.0;
    static constexpr double downbeatFreqHz = 1760.0;
    static constexpr double decaySeconds  = 0.02;
    static constexpr float  silentLevel   = 1.0e-4f;

    // Message → Audio
    std::atomic<bool> enabled { false };
    std::atomic<int>  anchor  { 0 };

    // Audio-Thread-State
    double currentSampleRate = 48000.0;
    float  envelope   = 0.0f;
    float  envelopeCoeff = 0.0f;
    double phase      = 0.0;
    double phaseInc   = 0.0;
    double previousBeat = 0.0;
    bool   beatValid  = false;  // erster Block nach prepare triggert nicht rückwirkend

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Metronome)
};

} // namespace conduit
