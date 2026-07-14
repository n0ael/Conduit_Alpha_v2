#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    Wiederverwendbares Gerüst eines rechts angedockten Editor-Panels
    (S2-Vorstufe MPE-Shaping) — bewusst content-agnostisch: eine Tab-Leiste
    oben, darunter ein Content-Host, der die Komponente des aktiven Tabs
    zeigt (genau ein aktiver Tab, alle anderen setVisible(false)). Links ein
    fingertauglicher Splitter-Griff (kSplitterWidth) zum Ziehen der
    Panel-Breite zwischen kMinWidth/kMaxWidth.

    Kennt weder GridVoiceEngine noch sonstige Engine-Typen — Besitzer ist
    seit MIDI-Rig M5b der EngineEditor (app-weit, Muster BrowserPanel);
    Content-Lieferanten (GridPage) hängen Inhalte über addTab ein und
    räumen sie im eigenen Destruktor per removeTab wieder ab (die Inhalte
    referenzieren Members ihres Lieferanten — Lebensdauer-Kopplung).

    Page-Masken (M5b, User-Entscheidung 14.07.2026): jeder Tab trägt eine
    Bitmaske über TransportBar::PageIndex — setActivePage() blendet die
    Tab-Buttons page-abhängig ein/aus (mpe/cc/macro/settings nur auf der
    Grid-Page, „Map" überall). Wird der aktive Tab unsichtbar, springt
    die Auswahl auf den ersten sichtbaren Tab (feuert onActiveTabChanged).

    getPreferredWidth() liefert 0 wenn geschlossen ODER wenn auf der
    aktuellen Page kein Tab sichtbar ist — das Parent-Layout
    (bounds.removeFromRight) reserviert dann keinen Platz. Message Thread.
*/
class EditorDockPanel final : public juce::Component
{
public:
    static constexpr int kMinWidth      = 220;
    static constexpr int kMaxWidth      = 480;
    static constexpr int kTabBarHeight  = 44;   // Touch-Target-Regel (CLAUDE.md 10)
    static constexpr int kSplitterWidth = 14;   // schmal, aber fingertauglich

    /** Page-Maske „auf allen Pages sichtbar" (alle Bits gesetzt). */
    static constexpr int kAllPages = -1;

    EditorDockPanel();

    /** Fügt einen Tab hinzu; der erste hinzugefügte Tab wird automatisch
        aktiv. content wird Kind des Content-Hosts. pageMask = Bitmaske
        über TransportBar::PageIndex (1 << page), Default: alle Pages. */
    void addTab (const juce::String& id, const juce::String& title,
                std::unique_ptr<juce::Component> content, int pageMask = kAllPages);

    /** Entfernt Tab + Content (kein Effekt bei unbekannter id). Feuert
        onActiveTabChanged NICHT — der Aufrufer ist typischerweise ein
        Destruktor (Content-Lieferant räumt ab), Callbacks in halb
        zerstörte Besitzer wären UB. War der Tab aktiv, wird still der
        erste sichtbare Tab aktiv. */
    void removeTab (const juce::String& id);

    /** Aktive Page (TransportBar::PageIndex) — blendet Tab-Buttons gemäß
        Page-Maske um; unsichtbar gewordener aktiver Tab wechselt auf den
        ersten sichtbaren (feuert onActiveTabChanged). */
    void setActivePage (int pageIndex);

    /** Schaltet auf den Tab mit dieser id (kein Effekt bei unbekannter id) —
        dessen Content wird sichtbar, alle anderen unsichtbar. */
    void setActiveTab (const juce::String& id);

    /** Id des aktiven Tabs — leer solange kein Tab existiert. */
    [[nodiscard]] juce::String getActiveTabId() const noexcept { return activeTabId; }

    /** Feuert bei jedem TATSÄCHLICHEN Wechsel des aktiven Tabs — auch beim
        Auto-Aktivieren des ersten addTab, falls der Callback dann schon
        gesetzt ist (GridPage verdrahtet ihn nach den addTab-Aufrufen und
        setzt den Initialzustand selbst). Unbekannte ids wechseln nichts
        und feuern nicht. */
    std::function<void (const juce::String&)> onActiveTabChanged;

    void setPanelOpen (bool shouldBeOpen) noexcept;
    [[nodiscard]] bool isPanelOpen() const noexcept { return open; }

    /** Setzt die Breite (geklemmt auf [kMinWidth, kMaxWidth]); ruft
        onWidthChanged bei tatsächlicher Änderung (Splitter-Live-Drag,
        initiales Laden aus der Persistenz). */
    void setPanelWidth (int newWidth) noexcept;
    [[nodiscard]] int getPanelWidth() const noexcept { return panelWidth; }

    /** Aktuelle Breite fürs Parent-Layout — 0 wenn geschlossen oder auf
        dieser Page kein Tab sichtbar (M5b). */
    [[nodiscard]] int getPreferredWidth() const noexcept
    {
        return open && visibleTabCount() > 0 ? panelWidth : 0;
    }

    /** Feuert bei jeder Breitenänderung (auch während des Drags) — der
        Besitzer legt sein Layout neu (Muster BrowserPanel::onDockWidthChanged). */
    std::function<void()> onWidthChanged;

    /** Feuert einmalig beim Loslassen des Splitters mit der neuen Breite —
        der Persistenz-Hook (GridPanelSettings). */
    std::function<void (int)> onWidthCommitted;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp   (const juce::MouseEvent& event) override;

private:
    struct TabEntry
    {
        juce::String id;
        std::unique_ptr<push::TextTile> button;
        std::unique_ptr<juce::Component> content;
        int pageMask = kAllPages;
    };

    [[nodiscard]] bool isTabVisibleOnPage (const TabEntry& tab) const noexcept
    {
        return (tab.pageMask & (1 << currentPageIndex)) != 0;
    }

    [[nodiscard]] int visibleTabCount() const noexcept;

    /** Button-/Content-Sichtbarkeit nach Page-Maske + aktivem Tab. */
    void applyTabVisibility();

    juce::Component contentHost;
    std::vector<TabEntry> tabs;
    juce::String activeTabId;

    int currentPageIndex = 0;

    bool open = false;
    int  panelWidth = kMinWidth;

    bool draggingSplitter = false;
    int  widthAtDragStart = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorDockPanel)
};

} // namespace conduit
