#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "TouchLive/TouchLiveClient.h"
#include "UI/PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    BROWSER-Sub-Tab der TouchLive-Page (M4, docs/TouchLive.md §7):
    Lives Browser im load_children-Muster — Wurzeln → Ordner → Items,
    Zeilen 44 px (Touch-Minimum), Breadcrumb mit Zurück-Chip.

    Interaktion: Tap auf Ordner → Kinder anfordern (Ebenen-Cache: Zurück
    braucht keinen Re-Request); Tap auf ladbares Item → Auswahl (bei
    aktivem PRE-Modus sofort Vorhören); Doppeltipp ODER LOAD-Kachel →
    /live/browser/load (lädt auf den in Live gewählten Track). Node-IDs
    sind Session-transient — nichts davon wird je persistiert.

    Die Antworten kommen über TouchLiveClient::onBrowserList (den die View
    exklusiv belegt); verlorene Antworten heilt der nächste Tap.
*/
class TouchLiveBrowserView final : public juce::Component
{
public:
    explicit TouchLiveBrowserView (TouchLiveClient& clientToUse);
    ~TouchLiveBrowserView() override;

    static constexpr int rowHeight = 44;
    static constexpr int headerHeight = 40;
    static constexpr int footerHeight = 40;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

    //==========================================================================
    // Testbare Kernpfade (Maus-Handler rufen genau diese)

    /** Zeile antippen: Ordner → hinein, ladbares Item → auswählen. */
    void tapRow (int rowIndex);

    /** Doppeltipp: ladbares Item sofort laden. */
    void doubleTapRow (int rowIndex);

    void goBack();
    void loadSelected();

    //==========================================================================
    [[nodiscard]] int getRowCount() const noexcept { return currentItems.size(); }
    [[nodiscard]] juce::String getRowName (int rowIndex) const;
    [[nodiscard]] int getSelectedNodeId() const noexcept { return selectedNodeId; }
    [[nodiscard]] int getDepth() const noexcept { return (int) levels.size(); }
    [[nodiscard]] bool isLoading() const noexcept { return loading; }

    push::TextTile backTile { "<" };
    push::TextTile loadTile { "LOAD", push::colours::ledGreen };
    push::TextTile previewTile { "PRE", push::colours::ledCyan };

private:
    //==========================================================================
    struct Level
    {
        int parentId = 0;
        juce::String name;      // Breadcrumb-Anteil
        juce::var items;        // Array aus [id, name, folder, loadable]
    };

    class ListArea;

    void handleBrowserList (int parentId, const juce::var& items);
    void requestLevel (int parentId, const juce::String& name);
    void refreshList();

    [[nodiscard]] juce::var rowEntry (int rowIndex) const;
    [[nodiscard]] static int entryId (const juce::var& entry);
    [[nodiscard]] static juce::String entryName (const juce::var& entry);
    [[nodiscard]] static bool entryIsFolder (const juce::var& entry);
    [[nodiscard]] static bool entryIsLoadable (const juce::var& entry);

    TouchLiveClient& client;

    std::vector<Level> levels;      // [0] = Wurzeln
    juce::Array<juce::var> currentItems;
    int pendingParentId = -1;       // wartende Anfrage (−1 = keine)
    juce::String pendingName;
    int selectedNodeId = -1;
    bool loading = false;
    bool previewMode = false;

    juce::Viewport viewport;
    std::unique_ptr<ListArea> listArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchLiveBrowserView)
};

} // namespace conduit
