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
    Reverse ◁). Animations-Phasen (Puls, Sweep) tickt der Besitzer —
    kein eigener Timer/VBlank pro Zelle: Struktur/Labels kommen vom
    15-Hz-Editor-Timer (setState), die Abspielposition monitor-synchron
    vom VBlank-Pfad des Editors (setProgress, User 09.07.2026).
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

    /** [Editor-VBlank] NUR die Loop-Phase nachziehen — der monitor-
        synchrone Sweep-Pfad: wirkt ausschließlich auf spielende Zellen
        (sonst no-op), der volle Zustand bleibt Sache von setState. */
    void setProgress (float progress01);

    /** [Editor, beim Commit] „Geschnappte" Strip-Ansicht des Clips
        (renderCommitThumbnail, Tinte auf transparent) + Quellfarbe: die
        Zelle malt ihre Fläche in der Quellfarbe und die Tinte schwarz
        darüber — die Strip-Optik invertiert (User-Idee 09.07.2026).
        sourceLabel (Quell-Text der Combo, z. B. „Live / wavetable") wird
        zum Zell-Label eingefroren — die Quelle des Loopers darf danach
        wechseln. clipId bindet Bild + Label an den Clip; der Editor-Timer
        räumt bei Mismatch auf (Delete, Überschreib-Commit, clearAllClips). */
    void setThumbnail (juce::Image inkImage, juce::Colour background, juce::uint32 clipId,
                       juce::String sourceLabel);
    void clearThumbnail();
    [[nodiscard]] bool hasThumbnail() const noexcept { return thumbnail.isValid(); }
    [[nodiscard]] juce::uint32 getThumbnailClipId() const noexcept { return thumbnailClipId; }
    [[nodiscard]] const juce::String& getThumbnailSourceLabel() const noexcept
    {
        return thumbnailSourceLabel;
    }

    // Papierkorb-Stash (Editor): Tinte + Quellfarbe zum Parken auslesen —
    // beim Restore setzt setThumbnail beides identisch wieder
    [[nodiscard]] const juce::Image& getThumbnailImage() const noexcept { return thumbnail; }
    [[nodiscard]] juce::Colour getThumbnailBackground() const noexcept
    {
        return thumbnailBackground;
    }

    /** Mittlere Tinten-Deckung (Alpha 0..1) einer normalisierten Zone des
        Tinte-Bildes — Aufbauten (Play-Dreieck, Label) nehmen auf dunklen
        Stellen die Gegenfarbe an und bleiben sichtbar (User 09.07.2026).
        Pure/static für Tests. */
    [[nodiscard]] static float computeInkCoverage (const juce::Image& ink,
                                                   juce::Rectangle<float> normalisedZone);

    /** [Page] Gemeinsame Puls-Phase 0..1 (Target-Pulsieren). */
    void setPulsePhase (float phase01);

    std::function<void()> onTap;

    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& event) override;

private:
    State state;
    float pulsePhase = 0.0f;

    juce::Image thumbnail;             // Tinte auf transparent (Strip-Snapshot)
    juce::Colour thumbnailBackground;  // Quellfarbe der Aufnahme
    juce::uint32 thumbnailClipId = 0;
    juce::String thumbnailSourceLabel; // beim Commit eingefrorener Quell-Text

    // Vorberechnete Tinten-Deckung der Kopfzeilen-Zonen (setThumbnail):
    // Dreieck-Ecke und volle Kopfzeile getrennt — paint() bleibt billig
    float iconZoneInk = 0.0f;
    float headlineZoneInk = 0.0f;

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

    /** Angezeigte Track-Nummer — GLOBAL im 4er-Raster (Looper 2 = Track
        5–8, `LooperPatchOutModule::globalTrackNumber`); der Editor setzt
        sie in refreshLooperStructure. */
    void setDisplayNumber (int number);

    //==========================================================================
    // Hooks [Page/Editor]
    std::function<void (float gain01)> onGainChanged;    // 0..1 (Unity = 1)
    std::function<void (float pan)> onPanChanged;        // −1..+1
    std::function<void (bool muted)> onMuteToggled;
    std::function<void (bool solo)> onSoloToggled;
    std::function<void()> onSendTileTapped;              // öffnet den Send-Dialog
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

    /** Send-Routing-Anzeige (Big Out): Kachel aktiv, sobald ein Send
        gesetzt ist; Label zeigt PRE-Abgriff mit an. */
    void setSendState (int sendMask, bool sendPre);

    /** [Editor-Timer] Post-Fader-Meter (RMS 0..1 pro Seite) + LED. */
    void setMeter (float rmsLeft, float rmsRight, bool audible);

    /** [Editor-Timer] Takt-Anzeige: „2 / 4" + Pie; leer = „gestoppt". */
    void setBarDisplay (int currentBar, int totalBars, float progress01);

    void setPulsePhase (float phase01);

    [[nodiscard]] push::TextTile& getMuteTile() noexcept { return muteTile; }
    [[nodiscard]] push::TextTile& getSoloTile() noexcept { return soloTile; }
    [[nodiscard]] push::TextTile& getSendTile() noexcept { return sndTile; }
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
    push::TextTile sndTile  { "SND", push::colours::ledGreen };
    push::TextTile stopTile { juce::String::fromUTF8 ("■"), push::colours::ledWhite };

    std::vector<std::unique_ptr<LooperSlotCell>> cells;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperTrackStrip)
};

} // namespace conduit
