#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GridPanelSettings.h"
#include "TouchLive/LiveSetModel.h"
#include "TrackSelectorPanel.h"

namespace conduit
{

//==============================================================================
/**
    Track-Tabs der Grid-Page (Block H3, User-Feedback-Runden 11.07.2026):
    alle Ableton-MIDI-Tracks in einer Zeile, Push-Optik — Nummer + Name in
    Jost in der Live-Track-Farbe, der Conduit-Fokus-Track grau unterlegt.

    Bedienung (Runde 3): der Wechsel braucht ein kurzes HALTEN
    (kSelectHoldMs — schützt vor versehentlichen Wechseln in der
    Performance); horizontales Ziehen SCROLLT den Strip, sobald die Tabs
    mit ihrer Mindestbreite (setMinTabWidth, Dev-Panel) nicht mehr in die
    Zeile passen (große Projekte). Schriftgröße einstellbar (setFontPx,
    Settings-Tab). Kernpfade beginPress/movePress/fireSelectTimeout/
    endPress sind headless testbar (Muster HoldIconTile).

    Datenquelle ist die tracks-Domain des LiveSetModel; der Besitzer ruft
    refresh() bei Domain-Änderungen (GridPage::refreshTrackFocus).
    Stable-IDs sind Laufzeit-IDs — NIE serialisieren. Message Thread.
*/
class TrackTabsStrip final : public juce::Component,
                             private juce::Timer
{
public:
    /** panelSettings: Schriftgröße (Settings-Tab) + Tab-Mindestbreite
        (Dev-Panel) werden pro VBlank gepollt (GridPanelSettings ist
        bewusst kein ChangeBroadcaster — Muster MpeShapingView::tick). */
    TrackTabsStrip (LiveSetModel& modelToUse, GridPanelSettings& panelSettingsToUse);

    /** Tab lange genug gehalten (Stable-ID) — Besitzer sendet das
        Fokus-Command. */
    std::function<void (const juce::String& stableKey)> onTrackChosen;

    /** Rows + Fokus aus der tracks-Domain neu lesen (repaint bei Delta). */
    void refresh();

    void setFontPx (int newFontPx);
    void setMinTabWidth (int newMinWidthPx);

    [[nodiscard]] int tabCount() const noexcept { return (int) rows.size(); }

    /** [Tests/Maus] Tab-Index an einer Komponenten-X-Position (inkl.
        Scroll-Offset), -1 = keiner. */
    [[nodiscard]] int tabIndexAt (int x) const noexcept;

    //==========================================================================
    // Testbare Kernpfade der Maus-Handler
    void beginPress (int x);
    void movePress (int totalDeltaX);
    void fireSelectTimeout();                 // = timerCallback-Kern
    void endPress();

    [[nodiscard]] int scrollOffset() const noexcept { return scrollOffsetPx; }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;

    static constexpr int kMaxTabWidth     = 220;
    static constexpr int kSelectHoldMs    = 300;
    static constexpr int kScrollTolerancePx = 8;

private:
    void timerCallback() override { fireSelectTimeout(); }

    [[nodiscard]] int tabWidth() const noexcept;
    [[nodiscard]] int contentWidth() const noexcept;
    void clampScroll();

    LiveSetModel& model;
    GridPanelSettings& panelSettings;
    std::vector<TrackSelectorPanel::TrackRow> rows;
    juce::String focusKey;

    juce::VBlankAttachment vblank { this, [this] (double)
    {
        setFontPx (panelSettings.getTrackTabsFontPx());
        setMinTabWidth (panelSettings.getTrackTabMinWidthPx());
    } };

    int fontPx = 12;
    int minTabWidthPx = 90;
    int scrollOffsetPx = 0;

    bool pressActive = false;
    bool scrolling = false;
    int  pressedTab = -1;
    int  scrollStartPx = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackTabsStrip)
};

} // namespace conduit
