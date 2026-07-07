#include "EditorDockPanel.h"

namespace conduit
{

EditorDockPanel::EditorDockPanel()
{
    addAndMakeVisible (contentHost);
    setVisible (open);   // Start geschlossen (open = false) -- Flag und Zustand synchron
}

void EditorDockPanel::addTab (const juce::String& id, const juce::String& title,
                              std::unique_ptr<juce::Component> content)
{
    TabEntry entry;
    entry.id     = id;
    entry.button = std::make_unique<push::TextTile> (title);
    entry.button->onClick = [this, id] { setActiveTab (id); };
    addAndMakeVisible (*entry.button);

    contentHost.addChildComponent (*content);
    entry.content = std::move (content);

    const auto becomesFirstTab = tabs.empty();
    tabs.push_back (std::move (entry));

    if (becomesFirstTab)
        setActiveTab (id);

    resized();
}

void EditorDockPanel::setActiveTab (const juce::String& id)
{
    for (auto& tab : tabs)
    {
        const auto isActive = tab.id == id;

        if (tab.content != nullptr)
            tab.content->setVisible (isActive);

        if (tab.button != nullptr)
            tab.button->setActive (isActive);
    }

    resized();
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

    auto tabBar = bounds.removeFromTop (kTabBarHeight);
    const auto tabWidth = tabs.empty() ? 0 : tabBar.getWidth() / (int) tabs.size();

    for (auto& tab : tabs)
        if (tab.button != nullptr)
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
