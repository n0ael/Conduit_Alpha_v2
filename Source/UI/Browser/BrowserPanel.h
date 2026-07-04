#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/Browser/BrowserModel.h"
#include "UI/AnimatedValue.h"
#include "UI/PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    Rechts angedocktes, touch-first Browser-Panel (CLAUDE.md 10) — öffnet
    über den Browser-Toggle der TransportBar und dockt im EngineEditor
    via bounds.removeFromRight (currentDockWidth()).

    Aufbau (oben → unten): Breadcrumb-Header (Zurück-Pfeil + Pfad, 44 px) ·
    virtualisierte Liste (juce::ListBox mit wiederverwendeten
    BrowserListRow-Komponenten, 44-px-Zeilen) · Suchfeld ganz UNTEN
    (Daumen-Erreichbarkeit; Live-Filter mit ~120 ms Debounce über den
    Hintergrund-Index, Escape löscht) · [TouchKeyboard folgt in M5].

    Das Panel kennt weder GraphManager noch Engine — Navigation macht das
    BrowserModel, Aktions-Zeilen laufen über die std::function-Hooks
    (Muster TransportBar). Slide-in/out über AnimatedValue (VBlank,
    Ease-Out ~180 ms): onDockWidthChanged → EngineEditor::resized().

    Ästhetik: Ableton-minimalistisch — monochrome Flächen, EIN Akzent
    (ledOrange, nur Selektion/LED), keine Hover-Pflicht (Touch!).
*/
class BrowserPanel final : public juce::Component,
                           private juce::ListBoxModel,
                           private juce::Timer   // Such-Debounce (~120 ms)
{
public:
    explicit BrowserPanel (BrowserModel& modelToUse);
    ~BrowserPanel() override;

    //==========================================================================
    static constexpr int dockWidth    = 320;
    static constexpr int rowHeight    = 44;   // Touch-Target-Regel (CLAUDE.md 10)
    static constexpr int headerHeight = 44;
    static constexpr int searchHeight = 44;
    static constexpr int searchDebounceMs = 120;

    /** Öffnet/schließt mit Slide-Animation; Öffnen navigiert zum
        Startbereich der aktiven Page (BrowserContextProvider). */
    void setOpen (bool shouldBeOpen, bool animate = true);
    [[nodiscard]] bool isOpen() const noexcept { return open; }

    /** Aktuelle Dock-Breite während der Animation (0 = zu). */
    [[nodiscard]] int currentDockWidth() const;

    /** Feuert bei jedem Animations-Frame — der Editor layoutet neu. */
    std::function<void()> onDockWidthChanged;

    //==========================================================================
    // Aktions-Hooks — vom EngineEditor verdrahtet (das Panel kennt weder
    // GraphManager noch Engine)

    /** Tap auf eine Modul-Zeile: factoryKey + Screen-Bounds der Zeile
        (Anker für Dialoge, z.B. Link-Send-Konfiguration). */
    std::function<void (const juce::String& factoryKey,
                        juce::Rectangle<int> rowScreenBounds)> onModuleActivated;

    /** Tap auf eine Aktions-Zeile (z.B. "load_preset"). */
    std::function<void (const juce::String& actionId)> onAction;

    //==========================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Test-Zugriff (read-only Verwendung). */
    [[nodiscard]] juce::ListBox& getListBox() noexcept { return list; }
    [[nodiscard]] push::IconTile& getBackTile() noexcept { return backTile; }
    [[nodiscard]] juce::TextEditor& getSearchField() noexcept { return searchField; }

    /** Test-Seam: Tap auf Zeile index (derselbe Pfad wie die Row-Geste). */
    void activateRowForTest (int rowIndex) { handleRowActivated (rowIndex); }

    /** Test-Seam: Debounce sofort auslösen (statt 120 ms zu warten). */
    void flushSearchDebounceForTest() { timerCallback(); }

private:
    // ListBoxModel — Zeilen sind Komponenten (refreshComponentForRow),
    // paintListBoxItem bleibt bewusst leer
    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int rowNumber, bool isRowSelected,
                                             juce::Component* existingComponentToUpdate) override;

    // juce::Timer — Such-Debounce
    void timerCallback() override;

    void handleRowActivated (int rowIndex);
    void refreshFromModel();
    void updateHeader();

    BrowserModel& model;

    push::IconTile backTile { push::Icon::chevronLeft, "browserBack" };
    juce::Label breadcrumbLabel;
    juce::ListBox list;
    juce::TextEditor searchField;
    juce::Rectangle<int> searchIconArea;   // Lupe links neben dem Feld (paint)

    AnimatedValue slide { *this };
    bool open = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserPanel)
};

} // namespace conduit
