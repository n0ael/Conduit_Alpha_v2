#pragma once

#include <functional>
#include <vector>

#include <juce_data_structures/juce_data_structures.h>

#include "Core/TransportSettings.h"
#include "ModuleBrowser.h"
#include "PushTiles.h"

namespace conduit
{

class LinkClock;

//==============================================================================
/**
    Transport-Header im Push-3-Stil (CLAUDE.md 10) — ersetzt die bisherige
    Modul-Button-Toolbar komplett (User-Entscheidung 2026-07-02).

    Layout (links → rechts):
      [▷ Play] [oo Tape] [⛶ Capture] · [Tap] [‹] [›] [○● Metronom] ·
      [Tempo 120.00] [Position] [Swing] [Link ▾] · … ·
      [Ω Grid] [∥∥ Mixer] [▷▭ Clip] [||| Device] · [+] [Undo] [Save] [⚙] ·
      [Root] [Skala]

    Zuständigkeiten:
      - Die Bar besitzt NUR UI-Zustand. Aktionen laufen über die
        std::function-Hooks (der EngineEditor verdrahtet Engine/UndoManager);
        Status kommt über refresh()/Setter vom Editor-Timer (15 Hz).
      - Skala schreibt direkt die Root-Tree-Properties (Session-Setting,
        bewusst ohne UndoManager — wie zuvor in der Toolbar).
      - Tempo: Drag (vertikal) und Doppelklick-Edit via LinkClock.
      - Browser („+"): Einträge injiziert der Editor (setBrowserItems).

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
    std::function<void()> onCaptureAll, onToggleCapturePanel;
    std::function<void (int pageIndex)> onPageSelected;   // Reihenfolge: pages[]

    /** Browser-Einträge (Module + Presets) — Anzeige-Reihenfolge = Liste. */
    void setBrowserItems (std::vector<ModuleBrowser::Item> items);

    //==========================================================================
    // Status — vom Editor-Timer gespeist [Message Thread]

    /** Pollt Tempo/Peer-Zahl aus der LinkClock (Repaint nur bei Änderung). */
    void refresh();

    void setCaptureStatus (bool recording, bool held, bool exporting);
    void setWarningText (const juce::String& warning);

    /** Positions-/Swing-Anzeige (Schritt 4 verdrahtet die Quellen). */
    void setPositionText (const juce::String& text);

    //==========================================================================
    static constexpr int preferredHeight = 56;

    /** Page-Reihenfolge in der Bar (Icon-Optik wie auf dem Push). */
    enum PageIndex { pageGrid = 0, pageMixer = 1, pageClip = 2, pageDevice = 3 };

    void setSelectedPage (int pageIndex);
    [[nodiscard]] int getSelectedPage() const noexcept { return selectedPage; }

    void paint (juce::Graphics& g) override;
    void resized() override;

    //==========================================================================
    // Test-/Editor-Zugriff auf einzelne Kacheln (read-only Verwendung)
    [[nodiscard]] push::IconTile& getPlayTile()     noexcept { return playTile; }
    [[nodiscard]] push::IconTile& getCaptureTile()  noexcept { return captureTile; }
    [[nodiscard]] push::IconTile& getPlusTile()     noexcept { return plusTile; }
    [[nodiscard]] push::TextTile& getUndoTile()     noexcept { return undoTile; }
    [[nodiscard]] push::TextTile& getLinkTile()     noexcept { return linkTile; }
    [[nodiscard]] push::ValueTile& getTempoTile()   noexcept { return tempoTile; }
    [[nodiscard]] push::TextTile& getAutomateTile()    noexcept { return automateTile; }
    [[nodiscard]] push::TextTile& getFixedLengthTile() noexcept { return fixedLengthTile; }
    [[nodiscard]] push::IconTile& getPageTile (int pageIndex) noexcept { return *pageTiles[(size_t) pageIndex]; }

private:
    void openBrowser();
    void openLinkMenu();
    void applyTempoText (const juce::String& entered);

    juce::ValueTree rootState;   // ref-counted Handle (Skala-Properties)
    LinkClock& linkClock;
    TransportSettings& transportSettings;

    // Transport links
    push::IconTile playTile     { push::Icon::play,         "play",     push::colours::ledGreen };
    push::IconTile tapeTile     { push::Icon::tapeLoop,     "tape",     push::colours::ledRed };
    push::IconTile captureTile  { push::Icon::captureFrame, "capture",  push::colours::ledOrange };
    push::TextTile fixedLengthTile { "Fixed Length" };
    push::TextTile automateTile    { "Automate", push::colours::ledRed };
    push::TextTile tapTile      { "Tap" };
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
    push::IconTile plusTile { push::Icon::plus, "plus" };
    push::TextTile undoTile { "Undo" };
    push::TextTile saveTile { "Save" };
    push::IconTile gearTile { push::Icon::gear, "settings" };
    juce::ComboBox rootCombo;
    juce::ComboBox scaleCombo;

    juce::Label warningLabel;

    std::vector<ModuleBrowser::Item> browserItems;
    int selectedPage = pageDevice;
    double tempoAtDragStart = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace conduit
