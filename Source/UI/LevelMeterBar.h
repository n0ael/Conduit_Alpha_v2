#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/Capture/LevelMeter.h"
#include "UiFramePacer.h"

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
    Frame-Tick (UiFramePacer: nativ per VBlank, global gedrosselt).
    Kanal-Index == Port-Index == Meter-Kanal (der Meter misst den
    komprimierten aktiven Buffer, CLAUDE.md 9).

    Maus: nur das Clip-Feld ist klickbar (resetClip) — der Rest fällt an die
    Kachel durch (Node-Drag). meter darf nullptr sein (Tests) → zeichnet nur
    den leeren Track (Tick ist ein No-op).
*/
class LevelMeterBar final : public juce::Component
{
public:
    /** lanesToShow = 2: kompakte STEREO-Variante (Looper-patch-OUT-Zeilen,
        User-Skizze 19.07.2026) — zwei dünne Lanes übereinander (L = channel,
        R = channel + 1), gemeinsames Clip-Feld (Klick resettet beide). */
    LevelMeterBar (LevelMeter* meterToUse, int channelToShow, int lanesToShow = 1);

    /** Teardown-Hook (Phase 1, 5.3): Rendering-Updates sofort stoppen. */
    void stopUpdates();

    void paint (juce::Graphics& g) override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent& event) override;

    /** dBFS-Mapping (−60…0 dB → 0…1) — pure, testbar. */
    [[nodiscard]] static float normFromLinear (float linearGain) noexcept;

    static constexpr float minDb = -60.0f;

private:
    void refreshTick();

    LevelMeter* meter = nullptr;
    const int channel;
    const int lanes;   // 1 = mono (Kanal-Zeile), 2 = kompakt stereo

    /** Eine Lane in ihr Teil-Rechteck malen (Werte-Index 0|1). */
    void paintLane (juce::Graphics& g, juce::Rectangle<float> usable, int lane);

    // Zwischengespeicherte ANZEIGE-Werte (dB-Norm 0..1) pro Lane für die
    // Change-Detection — der Vergleich muss im Anzeige-Maßstab passieren,
    // sonst wird die Schwelle unterhalb ~-36 dB riesig (0,002 linear ≈ 1 dB
    // dort; User-Feldtest 14.07.2026: Balken ruppelte im Keller).
    std::array<float, 2> lastRmsNorm {}, lastPeakNorm {}, lastHoldNorm {};
    bool lastClipped = false;   // Lane-übergreifend (gemeinsames Clip-Feld)

    // Letzter Member: tickt erst nach vollständiger Konstruktion.
    UiFramePacer framePacer { this, [this] { refreshTick(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeterBar)
};

} // namespace conduit
