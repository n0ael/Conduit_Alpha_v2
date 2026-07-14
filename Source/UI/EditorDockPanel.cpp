#include "EditorDockPanel.h"

#include <algorithm>

namespace conduit
{

EditorDockPanel::EditorDockPanel()
{
    addAndMakeVisible (contentHost);
    setVisible (open);   // Start geschlossen (open = false) -- Flag und Zustand synchron
}

void EditorDockPanel::addTab (const juce::String& id, const juce::String& title,
                              std::unique_ptr<juce::Component> content, int pageMask)
{
    TabEntry entry;
    entry.id       = id;
    entry.pageMask = pageMask;
    entry.button   = std::make_unique<push::TextTile> (title);
    entry.button->onClick = [this, id] { setActiveTab (id); };
    addAndMakeVisible (*entry.button);

    contentHost.addChildComponent (*content);
    entry.content = std::move (content);

    const auto becomesFirstTab = tabs.empty();
    tabs.push_back (std::move (entry));

    if (becomesFirstTab)
        setActiveTab (id);
    else
        applyTabVisibility();

    resized();
}

void EditorDockPanel::removeTab (const juce::String& id)
{
    const auto it = std::find_if (tabs.begin(), tabs.end(),
                                  [&id] (const TabEntry& tab) { return tab.id == id; });
    if (it == tabs.end())
        return;

    const auto wasActive = activeTabId == id;
    tabs.erase (it);

    // Still (kein onActiveTabChanged, s. Header): war der Tab aktiv, wird
    // der erste sichtbare Nachfolger aktiv.
    if (wasActive)
    {
        activeTabId.clear();
        for (const auto& tab : tabs)
        {
            if (isTabVisibleOnPage (tab))
            {
                activeTabId = tab.id;
                break;
            }
        }
    }

    applyTabVisibility();
    resized();
}

void EditorDockPanel::setActivePage (int pageIndex)
{
    currentPageIndex = juce::jlimit (0, 30, pageIndex);   // Bitmasken-sicher

    // Aktiver Tab auf dieser Page nicht sichtbar -> erster sichtbarer Tab
    // uebernimmt (feuert onActiveTabChanged wie ein User-Wechsel).
    const auto activeIt = std::find_if (tabs.begin(), tabs.end(),
                                        [this] (const TabEntry& tab) { return tab.id == activeTabId; });
    if (activeIt == tabs.end() || ! isTabVisibleOnPage (*activeIt))
    {
        for (const auto& tab : tabs)
        {
            if (isTabVisibleOnPage (tab))
            {
                setActiveTab (tab.id);
                return;   // setActiveTab ruft applyTabVisibility + resized
            }
        }
    }

    applyTabVisibility();
    resized();
}

int EditorDockPanel::visibleTabCount() const noexcept
{
    return (int) std::count_if (tabs.begin(), tabs.end(),
                                [this] (const TabEntry& tab) { return isTabVisibleOnPage (tab); });
}

void EditorDockPanel::applyTabVisibility()
{
    for (auto& tab : tabs)
    {
        const auto tabVisible = isTabVisibleOnPage (tab);
        const auto isActive   = tab.id == activeTabId;

        if (tab.button != nullptr)
        {
            tab.button->setVisible (tabVisible);
            tab.button->setActive (isActive);
        }

        if (tab.content != nullptr)
            tab.content->setVisible (isActive && tabVisible);
    }
}

void EditorDockPanel::setActiveTab (const juce::String& id)
{
    // Dokumentiertes Verhalten: kein Effekt (und kein Callback) bei
    // unbekannter id.
    const auto isKnown = std::any_of (tabs.begin(), tabs.end(),
                                      [&id] (const TabEntry& tab) { return tab.id == id; });
    if (! isKnown)
        return;

    const auto changed = activeTabId != id;
    activeTabId = id;

    applyTabVisibility();
    resized();

    if (changed && onActiveTabChanged != nullptr)
        onActiveTabChanged (activeTabId);
}

void EditorDockPanel::setPanelOpen (bool shouldBeOpen) noexcept
{
    if (shouldBeOpen == open)
        return;

    open = shouldBeOpen;
    setVisible (open);

    if (onWidthChanged != nullptr)
        onWidthChanged();
}

void EditorDockPanel::setPanelWidth (int newWidth) noexcept
{
    const auto clamped = juce::jlimit (kMinWidth, kMaxWidth, newWidth);

    if (clamped == panelWidth)
        return;

    panelWidth = clamped;

    if (onWidthChanged != nullptr)
        onWidthChanged();
}

void EditorDockPanel::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::panel);

    // Splitter-Griff: schmaler Streifen an der linken Kante, mit einem
    // zentrierten Grip-Strich als Ziehhinweis.
    const auto handle = getLocalBounds().removeFromLeft (kSplitterWidth).toFloat();
    g.setColour (push::colours::outline);
    g.fillRect (handle);
    g.setColour (push::colours::textDim);
    g.fillRoundedRectangle (handle.withSizeKeepingCentre (3.0f, 32.0f), 1.5f);
}

void EditorDockPanel::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromLeft (kSplitterWidth);

    // M5b: nur die auf der aktuellen Page sichtbaren Tabs teilen sich die
    // Leiste (versteckte Buttons behalten ihre alten Bounds -- unsichtbar).
    auto tabBar = bounds.removeFromTop (kTabBarHeight);
    const auto visibleTabs = visibleTabCount();
    const auto tabWidth = visibleTabs == 0 ? 0 : tabBar.getWidth() / visibleTabs;

    for (auto& tab : tabs)
        if (tab.button != nullptr && isTabVisibleOnPage (tab))
            tab.button->setBounds (tabBar.removeFromLeft (tabWidth));

    contentHost.setBounds (bounds);
    const auto contentBounds = contentHost.getLocalBounds();

    for (auto& tab : tabs)
        if (tab.content != nullptr)
            tab.content->setBounds (contentBounds);
}

void EditorDockPanel::mouseDown (const juce::MouseEvent& event)
{
    draggingSplitter = event.position.x < (float) kSplitterWidth;
    widthAtDragStart  = panelWidth;
}

void EditorDockPanel::mouseDrag (const juce::MouseEvent& event)
{
    if (! draggingSplitter)
        return;

    // Griff links an einem rechts angedockten Panel: nach LINKS ziehen
    // (negatives deltaX) vergrößert die Breite.
    setPanelWidth (widthAtDragStart - event.getDistanceFromDragStartX());
}

void EditorDockPanel::mouseUp (const juce::MouseEvent&)
{
    if (! draggingSplitter)
        return;

    draggingSplitter = false;

    if (onWidthCommitted != nullptr)
        onWidthCommitted (panelWidth);
}

} // namespace conduit
