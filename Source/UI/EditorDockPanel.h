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

    Kennt weder GridVoiceEngine noch sonstige Engine-Typen — der Besitzer
    (GridPage) hängt Inhalte über addTab ein und verdrahtet
    onWidthChanged/onWidthCommitted mit der Persistenz (Muster
    BrowserPanel::onDockWidthChanged).

    getPreferredWidth() liefert 0 wenn geschlossen — das Parent-Layout
    (bounds.removeFromRight) reserviert dann keinen Platz. Message Thread.
*/
class EditorDockPanel final : public juce::Component
{
public:
    static constexpr int kMinWidth      = 220;
    static constexpr int kMaxWidth      = 480;
    static constexpr int kTabBarHeight  = 44;   // Touch-Target-Regel (CLAUDE.md 10)
    static constexpr int kSplitterWidth = 14;   // schmal, aber fingertauglich

    EditorDockPanel();

    /** Fügt einen Tab hinzu; der erste hinzugefügte Tab wird automatisch
        aktiv. content wird Kind des Content-Hosts. */
    void addTab (const juce::String& id, const juce::String& title,
                std::unique_ptr<juce::Component> content);

    /** Schaltet auf den Tab mit dieser id (kein Effekt bei unbekannter id) —
        dessen Content wird sichtbar, alle anderen unsichtbar. */
    void setActiveTab (const juce::String& id);

    void setPanelOpen (bool shouldBeOpen) noexcept;
    [[nodiscard]] bool isPanelOpen() const noexcept { return open; }

    /** Setzt die Breite (geklemmt auf [kMinWidth, kMaxWidth]); ruft
        onWidthChanged bei tatsächlicher Änderung (Splitter-Live-Drag,
        initiales Laden aus der Persistenz). */
    void setPanelWidth (int newWidth) noexcept;
    [[nodiscard]] int getPanelWidth() const noexcept { return panelWidth; }

    /** Aktuelle Breite fürs Parent-Layout — 0 wenn geschlossen. */
    [[nodiscard]] int getPreferredWidth() const noexcept { return open ? panelWidth : 0; }

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
    };

    juce::Component contentHost;
    std::vector<TabEntry> tabs;

    bool open = false;
    int  panelWidth = kMinWidth;

    bool draggingSplitter = false;
    int  widthAtDragStart = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorDockPanel)
};

} // namespace conduit
