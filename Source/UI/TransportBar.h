#pragma once

#include <functional>
#include <vector>

#include <juce_data_structures/juce_data_structures.h>

#include "Core/TapTempo.h"
#include "Core/TransportSettings.h"
#include "PushTiles.h"
#include "Util/ScaleQuantizer.h"

namespace conduit
{

class LinkClock;

//==============================================================================
/**
    Transport-Header im Push-3-Stil (CLAUDE.md 10) — ersetzt die bisherige
    Modul-Button-Toolbar komplett (User-Entscheidung 2026-07-02).

    Layout (links → rechts):
      [▷ Play] [oo Tape] [⛶ Capture] · [Tap ▾] [Set] [‹] [›] [○● Metronom] ·
      [Tempo 120.00] [Position] [Swing] [Link ▾] · … ·
      [Ω Grid] [∥∥ Mixer] [▷▭ Clip] [||| Device] · [Undo] [Save] [⚙] ·
      [Root] [Skala] · [▯▮ Browser]

    Zuständigkeiten:
      - Die Bar besitzt NUR UI-Zustand. Aktionen laufen über die
        std::function-Hooks (der EngineEditor verdrahtet Engine/UndoManager);
        Status kommt über refresh()/Setter vom Editor-Timer (15 Hz).
      - Skala schreibt direkt die Root-Tree-Properties (Session-Setting,
        bewusst ohne UndoManager — wie zuvor in der Toolbar).
      - Tempo: Drag (vertikal) und Doppelklick-Edit via LinkClock.
      - Browser-Toggle (▯▮, außen rechts): klappt das rechts angedockte
        Browser-Panel auf/zu — das frühere „+" (ModuleBrowser-CallOutBox)
        ist im Panel aufgegangen (M3, 04.07.2026).

    Funktions-Staffelung des Meilensteins: Play/Link-Menü folgen in
    Schritt 3, Tap/Nudge/Position/Swing in Schritt 4, Metronom in
    Schritt 5, Page-Wechsel in Schritt 6 — bis dahin sind die betroffenen
    Kacheln sichtbar, aber disabled (Tooltip nennt den Stand).

    Undo-Kachel: Klick = Undo, Shift+Klick = Redo (Push-Konvention);
    Capture-Kachel: Klick = Capture alle, Shift+Klick = Kanal-Panel.
*/
class TransportBar final : public juce::Component
{
public:
    TransportBar (juce::ValueTree rootTree, LinkClock& linkClockToUse,
                  TransportSettings& transportSettingsToUse);

    //==========================================================================
    // Hooks — vom EngineEditor verdrahtet [Message Thread]
    std::function<void()> onUndo, onRedo, onSave, onSettings;
    std::function<void()> onToggleDevPanel;   // Dev-Tile (nur im Dev Mode sichtbar)
    std::function<void()> onCaptureAll, onToggleCapturePanel;
    std::function<void()> onToggleLooperPage;             // Tape-Kachel (oo)
    std::function<void()> onToggleBrowserPanel;           // Browser-Toggle rechts außen
    std::function<void()> onToggleEditorPanel;            // Grid-Editor-Dock-Panel-Toggle (S2)
    std::function<void (int pageIndex)> onPageSelected;   // Reihenfolge: pages[]

    /** Beschriftungen der Metronom-Ziel-Paare (Kanäle 2n/2n+1) fürs
        Link-Menü — der Editor liefert sie aus den ChannelNames. */
    std::function<juce::StringArray()> metronomeTargetNames;

    //==========================================================================
    // Status — vom Editor-Timer gespeist [Message Thread]

    /** Pollt Tempo/Peer-Zahl aus der LinkClock (Repaint nur bei Änderung). */
    void refresh();

    void setCaptureStatus (bool recording, bool held, bool exporting);
    void setWarningText (const juce::String& warning);

    /** DSP-Meter rechts neben der Warnung („DSP x % ⌀ / y % pk · N XRuns") —
        leerer String blendet aus (Editor gatet über UiSettings::dspMeter). */
    void setDspMeterText (const juce::String& statusText);

    /** Tape-LED (rot): Looper-Page offen ODER Loop spielt (B5). */
    void setLooperStatus (bool pageOpen, bool playing);

    /** Ableton-artige Positions-Anzeige „Takt. Beat. Sechzehntel" aus dem
        Session-Beat (Quantum 4) — public static für Tests. */
    [[nodiscard]] static juce::String formatPosition (double beatPosition);

    //==========================================================================
    static constexpr int preferredHeight = 56;

    /** Page-Reihenfolge in der Bar (Icon-Optik wie auf dem Push).
        pageLooper hat KEIN Page-Icon — die Looper-Page (B3) öffnet über
        die Tape-Kachel (oo) links im Transport. */
    enum PageIndex { pageGrid = 0, pageMixer = 1, pageClip = 2, pageDevice = 3,
                     pageLooper = 4 };

    void setSelectedPage (int pageIndex);
    [[nodiscard]] int getSelectedPage() const noexcept { return selectedPage; }

    void paint (juce::Graphics& g) override;
    void resized() override;

    //==========================================================================
    // Test-/Editor-Zugriff auf einzelne Kacheln (read-only Verwendung)
    [[nodiscard]] push::IconTile& getPlayTile()     noexcept { return playTile; }
    [[nodiscard]] push::IconTile& getTapeTile()     noexcept { return tapeTile; }
    [[nodiscard]] push::IconTile& getCaptureTile()  noexcept { return captureTile; }
    [[nodiscard]] push::TextTile& getUndoTile()     noexcept { return undoTile; }
    [[nodiscard]] push::TextTile& getLinkTile()     noexcept { return linkTile; }
    [[nodiscard]] push::ValueTile& getTempoTile()   noexcept { return tempoTile; }
    [[nodiscard]] push::ValueTile& getSwingTile()   noexcept { return swingTile; }
    [[nodiscard]] push::TextTile& getAutomateTile()    noexcept { return automateTile; }
    [[nodiscard]] push::TextTile& getFixedLengthTile() noexcept { return fixedLengthTile; }
    [[nodiscard]] push::IconTile& getPageTile (int pageIndex) noexcept { return *pageTiles[(size_t) pageIndex]; }

    /** Test-Seam: Tap mit injizierter Zeitbasis (Sekunden) — die Tap-Kachel
        ruft dieselbe Logik mit der echten Uhr. */
    void tapWithTime (double timeSeconds);

    /** Committet die getappte Preview zur Link-Session (Set-Kachel). */
    void commitTapPreview();

    /** Verwirft die Tap-Messung (UI: Tap gedrückt halten). */
    void resetTapMeasurement();

    [[nodiscard]] push::TextTile& getSetTile() noexcept { return setTile; }

    //==========================================================================
    /** Dev-Tile-Sichtbarkeit (app-weiter Dev Mode) — die Bar bleibt passiv,
        der EngineEditor setzt sie initial und in seinem UiSettings-Listener
        (Muster „Status kommt vom Editor"). */
    void setDevTileVisible (bool shouldBeVisible);

    /** LED des Dev-Tiles = Dev-Panel offen. */
    void setDevPanelOpen (bool isOpen);

    [[nodiscard]] push::TextTile& getDevTile() noexcept { return devTile; }
    [[nodiscard]] push::IconTile& getScaleToggleTile() noexcept { return scaleToggleTile; }

    //==========================================================================
    /** M7: Looper-Kontext — Save- und Delete-Kachel erscheinen NUR bei
        offener Looper-Page (User-Entscheidung 05.07.2026; Session-Save
        liegt im Browser). Die Gesten-Logik (Halten/Latch) verdrahtet der
        EngineEditor über die HoldTile-Hooks. */
    void setLooperPageContext (bool looperPageOpen);
    [[nodiscard]] bool isLooperPageContext() const noexcept { return looperContext; }

    [[nodiscard]] push::HoldTile& getSaveTile() noexcept { return saveTile; }
    [[nodiscard]] push::HoldTile& getLooperDeleteTile() noexcept { return looperDeleteTile; }

    /** LED des Browser-Toggles = Panel offen (Status kommt vom Editor). */
    void setBrowserPanelOpen (bool isPanelOpen);

    [[nodiscard]] push::IconTile& getBrowserPanelTile() noexcept { return browserPanelTile; }

    /** LED des Editor-Panel-Toggles = Grid-Editor-Dock-Panel offen (S2,
        unabhängig vom Browser -- Status kommt vom Editor). */
    void setEditorPanelOpen (bool isPanelOpen);

    [[nodiscard]] push::IconTile& getEditorPanelTile() noexcept { return editorPanelTile; }

private:
    void openLinkMenu();
    void openTapMenu();
    void applyTempoText (const juce::String& entered);
    void applySwingText (const juce::String& entered);
    void setGlobalSwing (double swing);
    [[nodiscard]] double getGlobalSwing() const;
    void handleNudge (push::IconTile& tile, bool& wasDown, double factor);

    juce::ValueTree rootState;   // ref-counted Handle (Skala-Properties)
    LinkClock& linkClock;
    TransportSettings& transportSettings;

    // Transport links
    push::IconTile playTile     { push::Icon::play,         "play",     push::colours::ledGreen };
    push::IconTile tapeTile     { push::Icon::tapeLoop,     "tape",     push::colours::ledRed };
    push::IconTile captureTile  { push::Icon::captureFrame, "capture",  push::colours::ledOrange };
    push::TextTile fixedLengthTile { "Fixed Length" };
    push::TextTile automateTile    { "Automate", push::colours::ledRed };
    push::TextTile tapTile      { "Tap", push::colours::ledCyan, true };
    push::TextTile setTile      { "Set", push::colours::ledCyan };
    push::IconTile nudgeDownTile { push::Icon::nudgeLeft,   "nudgeDown" };
    push::IconTile nudgeUpTile   { push::Icon::nudgeRight,  "nudgeUp" };
    push::IconTile metronomeTile { push::Icon::metronome,   "metronome", push::colours::ledWhite };

    // Mitte: Tempo/Position/Swing/Link
    push::ValueTile tempoTile    { "tempo" };
    push::ValueTile positionTile { "position" };
    push::ValueTile swingTile    { "swing" };
    push::TextTile linkTile      { "Link", push::colours::ledCyan, true };

    // Rechts: Pages + Aktionen + Skala
    std::vector<std::unique_ptr<push::IconTile>> pageTiles;
    push::TextTile undoTile { "Undo" };

    // M7: Save/Delete sind KONTEXT-Kacheln der Looper-Page (Halten + Ziel
    // antippen, Push-Muster); Session-Save wanderte in den Browser.
    push::HoldTile saveTile { "Save", push::colours::ledGreen };
    push::HoldTile looperDeleteTile { "Delete", push::colours::ledRed };
    bool looperContext = false;
    push::IconTile gearTile { push::Icon::gear, "settings" };
    push::TextTile devTile  { "Dev", push::colours::ledOrange };  // nur im Dev Mode

    // Skala-Gruppe im Ableton-Look: [♯-Toggle][Root][Skala] bündig — der
    // Toggle schaltet chromatic (= aus) ↔ letzte gewählte Skala
    push::IconTile scaleToggleTile { push::Icon::sharp, "scaleToggle", push::colours::ledWhite };
    juce::ComboBox rootCombo;
    juce::ComboBox scaleCombo;
    ScaleType lastNonChromaticScale = ScaleType::minor;   // transient (Session-Sitzung)

    // Browser-Panel-Toggle (Live-Icon gespiegelt, äußerstes Element rechts) —
    // Platzhalter: das rechts aufklappende Browser-Panel ist der nächste
    // Meilenstein (User 03.07.)
    push::IconTile browserPanelTile { push::Icon::browserPanel, "browserPanel",
                                      push::colours::ledOrange };

    // Grid-Editor-Dock-Panel-Toggle (S2-Vorstufe MPE-Shaping) -- eigenes
    // Icon (~, wiederverwendet aus dem FxModulePanel-CurveEditor-Button),
    // eigener Akzent, klar vom Browser-Toggle unterscheidbar.
    push::IconTile editorPanelTile { push::Icon::curve, "editorPanel",
                                     push::colours::ledCyan };

    juce::Label warningLabel;
    juce::Label dspMeterLabel;   // „DSP x % ⌀ / y % pk · N XRuns" (Timing-Monitor)

    int selectedPage = pageDevice;
    double tempoAtDragStart = 120.0;
    double swingAtDragStart = 0.0;

    // Tap-Monitor + Set-Commit (M4L-"TAP and CHANGE"-Modell) + Nudge
    TapTempo tapTempo;
    bool tapWasDown = false;        // Down-Flanken-Tracking (onStateChange)
    double tapHeldSince = -1.0;     // Gedrückthalten > Settings-Dauer = Reset
    bool tapHoldConsumed = false;   // ein Reset pro Halten
    bool nudgeUpWasDown = false, nudgeDownWasDown = false;
    double tempoBeforeNudge = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace conduit
