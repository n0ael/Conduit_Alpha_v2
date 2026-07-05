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
}

//==============================================================================
/**
    Clip-Controls-Leiste eines Loopers (M6, Übergabe §2): wirkt auf den
    AKTIV-Clip des Loopers. ×2 / ÷2 (nur Länge L) · ◁ Reverse · VARI-Knob
    (0.25×–4× log, Rastung bei 1×, Doppelklick = Reset, Rate-Anzeige) ·
    Frei/Gerastert-BUTTON daneben (User-Entscheidung — Rastmodus Halbtöne/
    Session-Skala kommt aus dem Menü) · „Sync"-Reset (Rate 1× + Anker aufs
    Commit-Raster) · TARGET (Kurzklick = Target-Track zyklisch weiter,
    Halten + Clip-Tap = Aktiv-Auswahl; versteckt bei 1 Track) ·
    Aktiv-Label rechts.

    Reine UI: Hooks nach oben, Zustand per Setter. Die Rast-Funktion
    (Halbtöne vs. Session-Skala) injiziert der Editor als snapFunction.
*/
class LooperClipControlsRow final : public juce::Component
{
public:
    LooperClipControlsRow();

    //==========================================================================
    // Hooks
    std::function<void()> onDoubleLength;
    std::function<void()> onHalveLength;
    std::function<void()> onReverseToggled;
    std::function<void (double rate)> onRateChanged;
    std::function<void (bool quantized)> onRasterToggled;
    std::function<void()> onResetWithSync;
    std::function<void()> onTargetCycle;                 // Kurzklick
    std::function<void (bool holding)> onTargetHold;     // Halten (Aktiv-Auswahl)

    //==========================================================================
    // Zustand [Editor]

    /** Controls aktiv/gedimmt (kein Aktiv-Clip → wirkungslos, Übergabe §3). */
    void setClipControlsEnabled (bool enabled);

    void setRate (double rate);                  // ohne Callback
    [[nodiscard]] double getRate() const noexcept { return currentRate; }

    void setReversed (bool reversed);
    void setRasterQuantized (bool quantized);
    void setTargetVisible (bool visible);        // versteckt bei 1 Track
    void setActiveLabel (const juce::String& label);   // "" = „kein Clip aktiv"

    /** Rast-Funktion des Knobs (Oktaven → Oktaven; Halbtöne oder
        Session-Skala) — greift nur bei gerastetem Zustand. */
    std::function<double (double octaves)> snapFunction;

    [[nodiscard]] push::TextTile& getDoubleTile() noexcept { return doubleTile; }
    [[nodiscard]] push::TextTile& getHalveTile() noexcept { return halveTile; }
    [[nodiscard]] push::TextTile& getReverseTile() noexcept { return reverseTile; }
    [[nodiscard]] push::TextTile& getRasterTile() noexcept { return rasterTile; }
    [[nodiscard]] push::TextTile& getSyncTile() noexcept { return syncTile; }
    [[nodiscard]] juce::Slider& getVariKnob() noexcept { return variKnob; }

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void updateRateLabel();
    void knobMoved();

    double currentRate = 1.0;
    bool controlsEnabled = false;
    bool rasterQuantized = false;

    push::TextTile doubleTile { juce::String::fromUTF8 ("×2") };
    push::TextTile halveTile { juce::String::fromUTF8 ("÷2") };
    push::TextTile reverseTile { juce::String::fromUTF8 ("◁"), push::colours::ledCyan };
    juce::Slider variKnob;
    juce::Label rateLabel;
    push::TextTile rasterTile { "Rast", push::colours::ledCyan };
    push::TextTile syncTile { "Sync" };
    push::HoldTile targetTile { "TARGET", push::colours::ledRed };
    juce::Label activeLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperClipControlsRow)
};

} // namespace conduit
