#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    Slot-Zelle des Clip-Grids (M6) — reine UI: Zustand kommt per Setter
    (Editor-Timer/Modell), Taps laufen als Hook nach oben. Zustände nach
    Übergabe-Dokument §3: leer / Target (rot pulsierend) / Clip
    (spielend mit Progress-Sweep + Play-Dreieck, gestoppt gedimmt);
    orthogonal: Aktiv-Kontur (Clip-Controls-Ziel), Badges (Rate ≠ 1×,
    Reverse ◁). Pulsieren über die vom Besitzer getickte Animations-Phase
    (setPulsePhase, Editor-Timer 30 Hz — kein eigener Timer pro Zelle).
*/
class LooperSlotCell final : public juce::Component
{
public:
    LooperSlotCell();

    struct State
    {
        bool hasClip = false;
        bool playing = false;
        bool target = false;
        bool active = false;       // Aktiv-Clip (helle Kontur)
        bool reversed = false;
        float progress01 = 0.0f;   // Loop-Phase (Sweep)
        juce::String label;        // "Clip 3 · 4 Bars"
        juce::String rateBadge;    // "0.71×" (leer bei 1×)
    };

    /** [Editor-Timer] Repaint nur bei sichtbarer Änderung. */
    void setState (const State& newState);
    [[nodiscard]] const State& getState() const noexcept { return state; }

    /** [Page] Gemeinsame Puls-Phase 0..1 (Target-Pulsieren). */
    void setPulsePhase (float phase01);

    std::function<void()> onTap;

    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& event) override;

private:
    State state;
    float pulsePhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperSlotCell)
};

//==============================================================================
/**
    Track-Spalte eines Loopers (M6, Design-Mock 05.07.2026): Header
    („TRACK n" + LED), Volume-Fader OBEN (VERTIKAL wischen — User-
    Entscheidung; Anzeige horizontal: Marker-Linie über Post-Fader-Meter),
    Pan-Zeile (Balance, horizontal ziehen, Doppelklick = Mitte), M/S-
    Kacheln, Slot-Zellen (sichtbare Zahl aus den LooperSettings), Stop-
    Kachel + Takt-Anzeige.

    Reine UI (Muster TransportBar): Aktionen als Hooks, Zustand per
    Setter. Der Editor persistiert in die LooperSettings; die Engine folgt
    über applyLooperSettings.
*/
class LooperTrackStrip final : public juce::Component
{
public:
    explicit LooperTrackStrip (int trackNumber);

    //==========================================================================
    // Hooks [Page/Editor]
    std::function<void (float gain01)> onGainChanged;    // 0..1 (Unity = 1)
    std::function<void (float pan)> onPanChanged;        // −1..+1
    std::function<void (bool muted)> onMuteToggled;
    std::function<void (bool solo)> onSoloToggled;
    std::function<void()> onStop;
    std::function<void (int slotIndex)> onSlotTapped;
    std::function<void()> onHeaderLongPress;             // Track entfernen
    std::function<void()> onHeaderTapped;                // M7: Delete-Geste + Header

    //==========================================================================
    // Zustand [Editor]

    void setVisibleSlots (int count);
    [[nodiscard]] int getVisibleSlots() const noexcept { return (int) cells.size(); }
    [[nodiscard]] LooperSlotCell& getSlotCell (int slotIndex);

    void setGain (float gain01);        // ohne Callback (persistierter Wert)
    [[nodiscard]] float getGain() const noexcept { return gain; }
    void setPan (float newPan);
    [[nodiscard]] float getPan() const noexcept { return pan; }
    void setMute (bool muted);
    void setSolo (bool solo);

    /** [Editor-Timer] Post-Fader-Meter (RMS 0..1 pro Seite) + LED. */
    void setMeter (float rmsLeft, float rmsRight, bool audible);

    /** [Editor-Timer] Takt-Anzeige: „2 / 4" + Pie; leer = „gestoppt". */
    void setBarDisplay (int currentBar, int totalBars, float progress01);

    void setPulsePhase (float phase01);

    [[nodiscard]] push::TextTile& getMuteTile() noexcept { return muteTile; }
    [[nodiscard]] push::TextTile& getSoloTile() noexcept { return soloTile; }
    [[nodiscard]] push::TextTile& getStopTile() noexcept { return stopTile; }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;

private:
    [[nodiscard]] juce::Rectangle<int> headerArea() const;
    [[nodiscard]] juce::Rectangle<int> faderArea() const;
    [[nodiscard]] juce::Rectangle<int> panArea() const;
    [[nodiscard]] juce::Rectangle<int> barArea() const;

    int trackNumber;

    float gain = 1.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    float meterLeft = 0.0f, meterRight = 0.0f;
    bool ledOn = false;

    int barCurrent = 0, barTotal = 0;
    float barProgress = 0.0f;

    // Drag-Zustand (Fader vertikal / Pan horizontal)
    enum class DragMode { none, fader, pan };
    DragMode dragMode = DragMode::none;
    float dragStartValue = 0.0f;
    juce::Point<int> dragStartPosition;
    juce::uint32 headerPressTime = 0;
    bool headerPressed = false;

    push::TextTile muteTile { "M", push::colours::ledOrange };
    push::TextTile soloTile { "S", push::colours::ledCyan };
    push::TextTile stopTile { juce::String::fromUTF8 ("■"), push::colours::ledWhite };

    std::vector<std::unique_ptr<LooperSlotCell>> cells;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperTrackStrip)
};

} // namespace conduit
