#pragma once

#include <functional>
#include <map>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/PageManager.h"

namespace conduit
{

//==============================================================================
/**
    Seiten-Selektion (ADR 008 M4, Ebene „Patch gesamt"): alle Seiten als
    Kacheln im gridX/gridY-Raster — leere Seiten GEDIMMT (nicht
    ausgeblendet), die aktive Seite markiert; Tap auf eine Kachel springt
    dorthin, das × auf leeren, nicht-aktiven Kacheln löscht sie
    (Regel a — die einzige Seiten-Lösch-UI).

    Miniaturen (Rendering-Invarianten ADR 008): gecachte Proxy-Bilder pro
    Seite (juce::Image, SCHEMATISCH — Node-Rechtecke + Kabellinien aus dem
    ValueTree; andere Seiten haben keine live-Components). Invalidierung
    via ValueTree-Listener, Neuaufbau VBlank-gesteuert max. EINE Miniatur
    pro Frame, NIE in paint() (paint blittet nur). Message Thread.
*/
class PageOverviewComponent final : public juce::Component,
                                    private juce::ValueTree::Listener
{
public:
    PageOverviewComponent (juce::ValueTree rootTree, PageManager& pageManagerToUse);
    ~PageOverviewComponent() override;

    /** Kachel-Tap → Seitenwechsel übernimmt der Besitzer (Canvas).
        Callbacks dürfen das Overlay zerstören — Aufruf IMMER über
        Stack-Kopie (UAF-Lektion 20.07.2026). */
    std::function<void (const juce::String& pageUuid)> onPageChosen;

    /** Hintergrund-Tap/Esc → schließen. Callbacks dürfen das Overlay
        zerstören — Aufruf IMMER über Stack-Kopie (UAF-Lektion 20.07.2026). */
    std::function<void()> onDismiss;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& event) override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** Kachel-Rechteck einer Seite (Tests + Hit-Testing); leer wenn
        unbekannt. */
    [[nodiscard]] juce::Rectangle<int> tileBoundsFor (const juce::String& pageUuid) const;

private:
    //==========================================================================
    // juce::ValueTree::Listener [Message Thread] — Miniaturen invalidieren
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int) override;
    void valueTreeRedirected (juce::ValueTree&) override;

    void markAllThumbnailsDirty();
    void markPageThumbnailDirty (const juce::String& pageUuid);

    /** VBlank-Tick: baut höchstens EINE dirty-Miniatur (Invariante). */
    void rebuildNextThumbnail();

    /** Schematische Proxy-Miniatur aus dem Tree (Rechtecke + Linien). */
    [[nodiscard]] juce::Image renderThumbnail (const juce::ValueTree& pageTree) const;

    void rebuildLayout();
    [[nodiscard]] bool pageHasNodes (const juce::String& pageUuid) const;

    //==========================================================================
    struct Tile
    {
        juce::String pageUuid;
        juce::Rectangle<int> bounds;
        bool empty = false;
        bool active = false;
    };

    juce::ValueTree rootState;
    PageManager& pageManager;

    std::vector<Tile> tiles;                          // Layout (resized/Tree-Änderung)
    std::map<juce::String, juce::Image> thumbnails;   // Cache pro pageUuid
    juce::StringArray dirtyThumbnails;                // Warteschlange für den VBlank
    std::unique_ptr<juce::VBlankAttachment> vblank;

    static constexpr int tileWidth  = 220;
    static constexpr int tileHeight = 150;
    static constexpr int tileGap    = 18;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageOverviewComponent)
};

} // namespace conduit
