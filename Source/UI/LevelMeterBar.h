#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/Capture/LevelMeter.h"

namespace conduit
{

//==============================================================================
/**
    Horizontale Pegelanzeige im Ableton-Stil für einen Kanal — lebt in der
    audio_in/audio_out-Kachel (NodeComponent). Zeigt kombiniert:
      - RMS als gefüllten Balken (stetig),
      - Peak als Marker-Linie (schnelle Ballistik),
      - Peak-Hold als dünnen Halte-Marker,
      - Clip als rotes Feld am 0-dB-Ende (Latch bis Klick).

    Liest die Werte lock-free vom LevelMeter (Owner: EngineProcessor); pro
    Frame (30 fps) via Timer. Kanal-Index == Port-Index == Meter-Kanal
    (der Meter misst den komprimierten aktiven Buffer, CLAUDE.md 9).

    Maus: nur das Clip-Feld ist klickbar (resetClip) — der Rest fällt an die
    Kachel durch (Node-Drag). meter darf nullptr sein (Tests) → zeichnet nur
    den leeren Track, kein Timer.
*/
class LevelMeterBar final : public juce::Component,
                            private juce::Timer
{
public:
    LevelMeterBar (LevelMeter* meterToUse, int channelToShow);

    /** Teardown-Hook (Phase 1, 5.3): Rendering-Updates sofort stoppen. */
    void stopUpdates();

    void paint (juce::Graphics& g) override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent& event) override;

    /** dBFS-Mapping (−60…0 dB → 0…1) — pure, testbar. */
    [[nodiscard]] static float normFromLinear (float linearGain) noexcept;

    static constexpr float minDb = -60.0f;

private:
    void timerCallback() override;

    LevelMeter* meter = nullptr;
    const int channel;

    // Zwischengespeicherte Werte für Change-Detection (repaint nur bei Bedarf)
    float lastRms = 0.0f, lastPeak = 0.0f, lastHold = 0.0f;
    bool lastClipped = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeterBar)
};

} // namespace conduit
