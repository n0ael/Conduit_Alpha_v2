#pragma once

#include <cmath>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PushTiles.h"

namespace conduit
{

//==============================================================================
/** Pure VARI-Mapping-Helfer (testbar ohne UI): der Knob läuft in OKTAVEN
    (−2..+2, log-symmetrisch um 1×), die Engine nimmt den Rate-Faktor. */
namespace looperui
{
    constexpr double variOctaveRange = 2.0;    // ±2 Oktaven = 0.25×–4×
    constexpr double variDetentOctaves = 0.08; // Fangbereich der 1×-Rastung

    [[nodiscard]] inline double rateFromOctaves (double octaves) noexcept
    {
        return std::pow (2.0, juce::jlimit (-variOctaveRange, variOctaveRange, octaves));
    }

    [[nodiscard]] inline double octavesFromRate (double rate) noexcept
    {
        return juce::jlimit (-variOctaveRange, variOctaveRange,
                             std::log2 (juce::jmax (1.0e-6, rate)));
    }

    /** Rastung bei 1× (kleiner Fangbereich um die Mitte). */
    [[nodiscard]] inline double applyDetent (double octaves) noexcept
    {
        return std::abs (octaves) < variDetentOctaves ? 0.0 : octaves;
    }

    /** Halbton-Raster (VariRaster::semitones). */
    [[nodiscard]] inline double snapToSemitones (double octaves) noexcept
    {
        return std::round (octaves * 12.0) / 12.0;
    }

    //==========================================================================
    // LEN/POS-Potis (07/2026): pure Mapping-Helfer Knob-Norm ↔ Wert

    constexpr double freeLenMinSeconds = 0.05;   // 50 ms
    constexpr double freeLenMaxSeconds = 60.0;

    /** Free-Modus: Knob 0..1 → Sekunden (log, 50 ms–60 s). */
    [[nodiscard]] inline double freeLenSecondsFromNorm (double norm01) noexcept
    {
        return freeLenMinSeconds
             * std::pow (freeLenMaxSeconds / freeLenMinSeconds,
                         juce::jlimit (0.0, 1.0, norm01));
    }

    [[nodiscard]] inline double freeLenNormFromSeconds (double seconds) noexcept
    {
        const auto clamped = juce::jlimit (freeLenMinSeconds, freeLenMaxSeconds, seconds);
        return std::log (clamped / freeLenMinSeconds)
             / std::log (freeLenMaxSeconds / freeLenMinSeconds);
    }

    /** Sync-Modus: Knob 0..1 rastet auf /8 /4 /2 /1 des Contents
        (0 = /8 kürzestes Fenster, 1 = voller Loop). */
    [[nodiscard]] inline double syncFractionFromNorm (double norm01) noexcept
    {
        const auto index = (int) std::lround (juce::jlimit (0.0, 1.0, norm01) * 3.0);
        return 1.0 / static_cast<double> (1 << (3 - index));
    }

    [[nodiscard]] inline double syncNormFromFraction (double fraction) noexcept
    {
        const auto clamped = juce::jlimit (0.125, 1.0, fraction);
        const auto index = 3.0 + std::log2 (clamped);   // /8→0 … /1→3
        return juce::jlimit (0.0, 1.0, index / 3.0);
    }
}

//==============================================================================
/**
    Clip-Controls-Leiste eines Loopers (M6; LEN/POS-Umbau 07/2026):
    wirkt auf den AKTIV-Clip. Sync/Free-Toggle (Takt-Raster vs.
    stufenlos) · LEN-Poti (Sync: /8 /4 /2 /1 des Clips · Free:
    50 ms–60 s, DK = voller Loop) · POS-Poti (Loop-Fenster verschieben,
    DK = Reset — ersetzt die alten ×2/÷2-Kacheln) · ◁ Reverse ·
    VARI-Poti (0.25×–4×, Rastung bei 1×, DK = Reset) · Tape/Quant-
    Toggle · TGT (Kurzklick = Target-Track weiter, Halten + Clip-Tap =
    Aktiv-Auswahl; versteckt bei 1 Track) · Aktiv-Label rechts. ReSet
    (Rate 1× + Re-Sync) lebt jetzt am Footer-Play (Long-Press).

    Reine UI: Hooks nach oben (LEN/POS liefern die Knob-NORM 0..1 —
    der Editor rechnet mit Clip-Info in Beats um), Zustand per Setter.
    Rast-Funktion (Halbtöne/Session-Skala) und Rate-Anzeige-Formatter
    („+3 st" vs. „♭3", VARI Display) injiziert der Editor.
*/
class LooperClipControlsRow final : public juce::Component
{
public:
    LooperClipControlsRow();

    //==========================================================================
    // Hooks
    std::function<void (bool sync)> onSyncFreeToggled;
    std::function<void (double norm01, bool sync)> onLoopLenChanged;
    std::function<void (double norm01, bool sync)> onLoopPosChanged;
    std::function<void()> onReverseToggled;
    std::function<void (double rate)> onRateChanged;
    std::function<void (bool quantized)> onRasterToggled;   // Tape/Quant
    std::function<void()> onTargetCycle;                 // Kurzklick
    std::function<void (bool holding)> onTargetHold;     // Halten (Aktiv-Auswahl)

    //==========================================================================
    // Zustand [Editor]

    /** Controls aktiv/gedimmt (kein Aktiv-Clip → wirkungslos, Übergabe §3). */
    void setClipControlsEnabled (bool enabled);

    void setRate (double rate);                  // ohne Callback
    [[nodiscard]] double getRate() const noexcept { return currentRate; }

    void setReversed (bool reversed);
    void setRasterQuantized (bool quantized);    // Tape (frei) / Quant
    void setTargetVisible (bool visible);        // versteckt bei 1 Track
    void setActiveLabel (const juce::String& label);   // "" = „kein Clip aktiv"

    /** Sync/Free-Zustand + LEN/POS-Knob-Positionen (ohne Callback). */
    void setSyncFree (bool sync);
    [[nodiscard]] bool isSyncMode() const noexcept { return syncMode; }
    void setLoopLenNorm (double norm01, const juce::String& display);
    void setLoopPosNorm (double norm01, const juce::String& display);

    /** Rast-Funktion des Knobs (Oktaven → Oktaven; Halbtöne oder
        Session-Skala) — greift nur bei gerastetem Zustand. */
    std::function<double (double octaves)> snapFunction;

    /** Rate-Anzeige (VARI Display): rate → Text; null = „x.xx×". */
    std::function<juce::String (double rate)> rateFormatter;

    [[nodiscard]] push::TextTile& getSyncFreeTile() noexcept { return syncFreeTile; }
    [[nodiscard]] push::TextTile& getReverseTile() noexcept { return reverseTile; }
    [[nodiscard]] push::TextTile& getRasterTile() noexcept { return rasterTile; }
    [[nodiscard]] juce::Slider& getVariKnob() noexcept { return variKnob; }
    [[nodiscard]] juce::Slider& getLenKnob() noexcept { return lenKnob; }
    [[nodiscard]] juce::Slider& getPosKnob() noexcept { return posKnob; }

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void updateRateLabel();
    void knobMoved();

    double currentRate = 1.0;
    bool controlsEnabled = false;
    bool rasterQuantized = false;
    bool syncMode = true;

    push::TextTile syncFreeTile { "Sync" };
    juce::Slider lenKnob, posKnob;
    juce::Label lenLabel, posLabel;
    push::TextTile reverseTile { juce::String::fromUTF8 ("◁"), push::colours::ledCyan };
    juce::Slider variKnob;
    juce::Label rateLabel;
    push::TextTile rasterTile { "Tape", push::colours::ledCyan };
    push::HoldTile targetTile { "TGT", push::colours::ledRed };
    juce::Label activeLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperClipControlsRow)
};

} // namespace conduit
