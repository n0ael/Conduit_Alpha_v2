#pragma once

#include <array>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PushTiles.h"

namespace conduit
{

namespace looperui
{
    /** Werks-Palette der Sends S1–S4 (● ■ ▲ ⬡) — die AKTUELLEN Farben
        sind seit 20.07.2026 frei wählbar und kommen per Setter aus den
        LooperSettings; diese Tabelle ist nur noch der Anzeige-Fallback,
        solange nichts gesetzt wurde. */
    [[nodiscard]] inline juce::Colour sendColour (int sendIndex) noexcept
    {
        switch (sendIndex)
        {
            case 0:  return juce::Colour (0xffff8d28);
            case 1:  return juce::Colour (0xff6155f5);
            case 2:  return juce::Colour (0xff34c759);
            default: return juce::Colour (0xff00c8b3);
        }
    }

    using SendColours = std::array<juce::Colour, 4>;

    [[nodiscard]] inline SendColours defaultSendColours() noexcept
    {
        return { sendColour (0), sendColour (1), sendColour (2), sendColour (3) };
    }
}

//==============================================================================
/**
    XY-Panner des Track-Mixers (07/2026, Geometrie-Referenz „looper mixer
    dark"-SVGs): X = Pan (L/C/R), Y = Distanz („FAR" oben). Der Puck
    zeigt die Send-Farben des Tracks als Misch-Overlay (Alpha = Level).
    Doppelklick = Reset (Center, Distanz 0). Kompakt-Variante (~56 px)
    ohne FAR-Label und mit kleinerem Puck (3–4 Tracks pro Looper).
    Reine UI: absolute Puck-Positionierung beim Ziehen, Hooks nach oben.
*/
class LooperXyPad final : public juce::Component
{
public:
    LooperXyPad() { setName ("looperXyPad"); }

    std::function<void (float pan, float distance01)> onChanged;

    void setValues (float pan, float distance01);
    [[nodiscard]] float getPan() const noexcept { return panValue; }
    [[nodiscard]] float getDistance() const noexcept { return distanceValue; }

    void setCompact (bool shouldBeCompact);
    void setSendLevels (const std::array<float, 4>& levels, int visibleSendCount);
    void setSendColours (const looperui::SendColours& colours);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;

private:
    /** Wischweg für den vollen Regelweg als Vielfaches der Pad-Größe
        (User 20.07.2026: „2 bis 3 mal so groß" ohne größeres UI). */
    static constexpr float dragTravelFactor = 2.5f;

    float panValue = 0.0f;        // −1..+1
    float distanceValue = 0.0f;   // 0..1, „FAR" = 1
    bool compact = false;
    std::array<float, 4> sendLevels {};
    looperui::SendColours sendColours = looperui::defaultSendColours();
    int sendCount = 4;

    // Relatives Ziehen: Startwerte beim Aufsetzen
    float dragStartPan = 0.0f;
    float dragStartDistance = 0.0f;
    juce::Point<float> dragStartPosition;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperXyPad)
};

//==============================================================================
/**
    Send-Kachel S1–S4 (07/2026): Form-Icon (● ■ ▲ ⬡) in der Send-Farbe,
    Füllstand von unten = Send-Level; vertikal ziehen, Doppelklick = 0.
    Optionales Text-Label „S1…" (nur bei 1 Track pro Looper); Y-Link-
    Markierung als kleiner Punkt. 38-px-Optik in ≥44-px-Hit-Fläche.
*/
class LooperSendTile final : public juce::Component
{
public:
    explicit LooperSendTile (int sendIndexToUse);

    std::function<void (float level01)> onLevelChanged;

    void setLevel (float level01);
    [[nodiscard]] float getLevel() const noexcept { return level; }
    void setColour (juce::Colour newColour);
    void setShowLabel (bool show);
    void setYLinked (bool linked);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;

private:
    [[nodiscard]] juce::Path shapePath (juce::Rectangle<float> bounds) const;

    int sendIndex;
    juce::Colour colour = looperui::sendColour (0);
    float level = 0.0f;
    bool showLabel = false;
    bool yLinked = false;
    float dragStartLevel = 0.0f;
    juce::Point<int> dragStartPosition;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperSendTile)
};

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

        // LEN/POS (07/2026): Loop-Fenster im Content — der NICHT loopende
        // Teil dunkelt ab, „/n"-Badge bei gerasterter Länge
        float loopStart01 = 0.0f;
        float loopLen01 = 1.0f;
        juce::String divBadge;     // "/2" … (leer bei vollem Loop/Free)
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
    Track-Spalte eines Loopers (M6; Mixer-Umbau 07/2026): Header
    („TRACK n" global + LED), Mitte-raus-STEREO-METER (RMS-Fläche +
    Peak-Linie pro Seite, post-fader = bereits pan-gewichtet; VERTIKAL
    wischen = Gain wie der frühere Fader), XY-Panner (X = Pan, Y =
    Distanz), Send-Kacheln S1–S4 (Level von unten), M/S, Slot-Zellen,
    Stop-Kachel + Takt-Anzeige. Die frühere Fader-/Pan-Zeile und die
    SND-Kachel sind ERSETZT; Mute+Solo und XY sind global ausblendbar
    (DISPLAY-Optionen — „erst mappen, dann verstecken").

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
    std::function<void (float pan)> onPanChanged;        // −1..+1 (XY-X)
    std::function<void (float distance01)> onDistanceChanged;   // XY-Y („FAR" = 1)
    std::function<void (int sendIndex, float level01)> onSendLevelChanged;
    std::function<void (bool muted)> onMuteToggled;
    std::function<void (bool solo)> onSoloToggled;
    std::function<void()> onPlay;        // ▶ startet aktiven/ersten belegten Slot
    std::function<void()> onResetSync;   // Long-Press auf ▶ = ReSet (Rate 1×, Re-Sync)
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
    void setDistance (float distance01);
    [[nodiscard]] float getDistance() const noexcept { return distance; }
    void setMute (bool muted);
    void setSolo (bool solo);

    /** Send-Level S1–S4 (Kachel-Füllstand + Puck-Farb-Overlay). */
    void setSendLevels (const std::array<float, 4>& levels);
    /** Sichtbare Send-Anzahl (SENDS · GLOBAL, 0 = keine Kacheln). */
    void setSendCount (int count);
    /** Y-Link-Markierung (−1 = keiner). */
    void setYLinkSend (int sendIndex);
    /** Send-Farben (frei wählbar, LooperSettings) — Kacheln + Puck. */
    void setSendColours (const looperui::SendColours& colours);
    /** Text-Labels „S1…" nur bei genau 1 Track pro Looper. */
    void setSendLabelsVisible (bool visible);

    // DISPLAY-Optionen + Kompakt-Layout (Panel setzt nach Track-Zahl)
    void setShowMuteSolo (bool show);
    void setShowXy (bool show);
    void setXyCompact (bool compact);

    /** [Editor-Timer] Post-Fader-Meter (RMS + Peak 0..1 pro Seite) + LED. */
    void setMeter (float rmsLeft, float rmsRight,
                   float peakLeft, float peakRight, bool audible);

    /** [Editor-Timer] Takt-Anzeige: „2 / 4" + Pie; leer = „gestoppt". */
    void setBarDisplay (int currentBar, int totalBars, float progress01);

    void setPulsePhase (float phase01);

    [[nodiscard]] push::TextTile& getMuteTile() noexcept { return muteTile; }
    [[nodiscard]] push::TextTile& getSoloTile() noexcept { return soloTile; }
    [[nodiscard]] push::TextTile& getStopTile() noexcept { return stopTile; }
    [[nodiscard]] push::HoldTile& getPlayTile() noexcept { return playTile; }
    [[nodiscard]] LooperXyPad& getXyPad() noexcept { return xyPad; }
    [[nodiscard]] LooperSendTile& getSendTile (int sendIndex);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;

private:
    [[nodiscard]] juce::Rectangle<int> headerArea() const;
    [[nodiscard]] juce::Rectangle<int> meterArea() const;
    [[nodiscard]] juce::Rectangle<int> barArea() const;
    [[nodiscard]] int xyHeight() const noexcept;
    [[nodiscard]] int sendRowHeight() const noexcept;
    void pushSendStateToChildren();

    int trackNumber;

    float gain = 1.0f;
    float pan = 0.0f;
    float distance = 0.0f;
    bool mute = false;
    bool solo = false;
    float meterLeft = 0.0f, meterRight = 0.0f;
    float peakLeft = 0.0f, peakRight = 0.0f;
    bool ledOn = false;

    std::array<float, 4> sendLevels {};
    looperui::SendColours sendColours = looperui::defaultSendColours();
    int sendCount = 4;
    int yLinkSend = -1;
    bool sendLabels = false;
    bool showMuteSolo = true;
    bool showXy = true;
    bool xyCompact = false;

    int barCurrent = 0, barTotal = 0;
    float barProgress = 0.0f;

    // Drag-Zustand (Gain vertikal auf der Meter-Zeile)
    enum class DragMode { none, gain };
    DragMode dragMode = DragMode::none;
    float dragStartValue = 0.0f;
    juce::Point<int> dragStartPosition;
    juce::uint32 headerPressTime = 0;
    bool headerPressed = false;

    push::TextTile muteTile { "M", push::colours::ledOrange };
    push::TextTile soloTile { "S", push::colours::ledCyan };
    push::TextTile stopTile { juce::String::fromUTF8 ("■"), push::colours::ledWhite };
    push::HoldTile playTile { juce::String::fromUTF8 ("▶"), push::colours::ledGreen };
    LooperXyPad xyPad;
    std::array<std::unique_ptr<LooperSendTile>, 4> sendTiles;

    std::vector<std::unique_ptr<LooperSlotCell>> cells;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperTrackStrip)
};

} // namespace conduit
