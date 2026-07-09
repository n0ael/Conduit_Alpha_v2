#include "TouchLiveBrowserView.h"

#include "UI/PushIcons.h"
#include "UI/PushLookAndFeel.h"

namespace conduit
{

//==============================================================================
/** Zeilen-Fläche im Viewport: paint-only (kein Component pro Zeile),
    Tap/Doppeltipp gehen an die View. */
class TouchLiveBrowserView::ListArea final : public juce::Component
{
public:
    explicit ListArea (TouchLiveBrowserView& ownerToUse) : owner (ownerToUse) {}

    void paint (juce::Graphics& g) override
    {
        g.setFont (push::scaledFont (14.0f));

        for (int row = 0; row < owner.getRowCount(); ++row)
        {
            const auto entry = owner.rowEntry (row);
            const auto bounds = juce::Rectangle<int> (0, row * rowHeight,
                                                      getWidth(), rowHeight);
            const auto selected = entryId (entry) == owner.selectedNodeId;

            if (selected)
            {
                g.setColour (push::colours::tileActive);
                g.fillRoundedRectangle (bounds.toFloat().reduced (2.0f, 1.0f), 4.0f);
            }

            // Marker: › für Ordner, ● für ladbare Items
            const auto marker = bounds.toFloat().removeFromLeft (34.0f);

            if (entryIsFolder (entry))
            {
                juce::Path chevron;
                chevron.startNewSubPath (marker.getCentreX() - 3.0f, marker.getCentreY() - 6.0f);
                chevron.lineTo (marker.getCentreX() + 4.0f, marker.getCentreY());
                chevron.lineTo (marker.getCentreX() - 3.0f, marker.getCentreY() + 6.0f);
                g.setColour (push::colours::textDim);
                g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                             juce::PathStrokeType::rounded));
            }
            else if (entryIsLoadable (entry))
            {
                g.setColour (push::colours::ledGreen.withAlpha (0.8f));
                g.fillEllipse (marker.withSizeKeepingCentre (7.0f, 7.0f));
            }

            g.setColour (selected ? push::colours::text
                                  : push::colours::text.withAlpha (0.85f));
            g.drawFittedText (entryName (entry),
                              bounds.withTrimmedLeft (36).reduced (4, 0),
                              juce::Justification::centredLeft, 1, 1.0f);

            g.setColour (push::colours::outline.withAlpha (0.35f));
            g.fillRect (0, bounds.getBottom() - 1, getWidth(), 1);
        }
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (event.mouseWasDraggedSinceMouseDown())
            return;

        owner.tapRow (event.y / rowHeight);
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        owner.doubleTapRow (event.y / rowHeight);
    }

private:
    TouchLiveBrowserView& owner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ListArea)
};

//==============================================================================
TouchLiveBrowserView::TouchLiveBrowserView (TouchLiveClient& clientToUse)
    : client (clientToUse)
{
    listArea = std::make_unique<ListArea> (*this);
    viewport.setViewedComponent (listArea.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (12);
    addAndMakeVisible (viewport);

    backTile.onClick = [this] { goBack(); };
    addAndMakeVisible (backTile);

    loadTile.setTooltip (juce::String::fromUTF8 (
        "Auswahl laden — Ziel ist der in Live gewählte Track"));
    loadTile.onClick = [this] { loadSelected(); };
    addAndMakeVisible (loadTile);

    previewTile.setTooltip (juce::String::fromUTF8 (
        "Vorhören: angetippte Items sofort anspielen"));
    previewTile.onClick = [this]
    {
        previewMode = ! previewMode;
        previewTile.setActive (previewMode);

        if (! previewMode)
            client.stopBrowserPreview();
    };
    addAndMakeVisible (previewTile);

    // Antworten der Gegenseite — die View belegt den Callback exklusiv
    client.onBrowserList = [this] (int parentId, juce::var items)
    { handleBrowserList (parentId, items); };
}

TouchLiveBrowserView::~TouchLiveBrowserView()
{
    client.onBrowserList = nullptr;
}

//==============================================================================
void TouchLiveBrowserView::visibilityChanged()
{
    if (isVisible() && ! rootsRequested)
    {
        rootsRequested = true;
        requestLevel (0, {});
    }
}

void TouchLiveBrowserView::requestLevel (int parentId, const juce::String& name)
{
    pendingParentId = parentId;
    pendingName = name;
    loading = true;
    repaint();

    if (parentId == 0)
        client.requestBrowserRoots();
    else
        client.requestBrowserChildren (parentId);
}

void TouchLiveBrowserView::handleBrowserList (int parentId, const juce::var& items)
{
    if (parentId != pendingParentId)
        return;   // veraltete Antwort (Navigation war schneller)

    Level level;
    level.parentId = parentId;
    level.name = pendingName;
    level.items = items;

    // Wurzeln ersetzen den Stack, alles andere stapelt
    if (parentId == 0)
        levels.clear();

    levels.push_back (std::move (level));

    pendingParentId = -1;
    loading = false;
    selectedNodeId = -1;
    refreshList();
}

void TouchLiveBrowserView::refreshList()
{
    currentItems.clear();

    if (! levels.empty())
        if (const auto* array = levels.back().items.getArray())
            currentItems = *array;

    listArea->setSize (juce::jmax (1, viewport.getMaximumVisibleWidth()),
                       juce::jmax (1, getRowCount() * rowHeight));
    viewport.setViewPosition (0, 0);
    backTile.setEnabled (levels.size() > 1);
    repaint();
}

//==============================================================================
void TouchLiveBrowserView::tapRow (int rowIndex)
{
    const auto entry = rowEntry (rowIndex);

    if (entry.isVoid())
        return;

    if (entryIsFolder (entry))
    {
        requestLevel (entryId (entry), entryName (entry));
        return;
    }

    if (entryIsLoadable (entry))
    {
        selectedNodeId = entryId (entry);
        listArea->repaint();

        if (previewMode)
            client.previewBrowserItem (selectedNodeId);
    }
}

void TouchLiveBrowserView::doubleTapRow (int rowIndex)
{
    const auto entry = rowEntry (rowIndex);

    if (! entry.isVoid() && entryIsLoadable (entry))
    {
        selectedNodeId = entryId (entry);
        client.loadBrowserItem (selectedNodeId);
    }
}

void TouchLiveBrowserView::goBack()
{
    if (levels.size() <= 1)
        return;

    levels.pop_back();   // Ebenen-Cache: kein Re-Request nötig
    pendingParentId = -1;
    loading = false;
    selectedNodeId = -1;
    refreshList();
}

void TouchLiveBrowserView::loadSelected()
{
    if (selectedNodeId >= 0)
        client.loadBrowserItem (selectedNodeId);
}

//==============================================================================
juce::var TouchLiveBrowserView::rowEntry (int rowIndex) const
{
    return rowIndex >= 0 && rowIndex < currentItems.size()
               ? currentItems[rowIndex] : juce::var();
}

juce::String TouchLiveBrowserView::getRowName (int rowIndex) const
{
    return entryName (rowEntry (rowIndex));
}

int TouchLiveBrowserView::entryId (const juce::var& entry)
{
    const auto* array = entry.getArray();
    return array != nullptr && array->size() > 0 ? (int) array->getReference (0) : -1;
}

juce::String TouchLiveBrowserView::entryName (const juce::var& entry)
{
    const auto* array = entry.getArray();
    return array != nullptr && array->size() > 1 ? array->getReference (1).toString()
                                                 : juce::String();
}

bool TouchLiveBrowserView::entryIsFolder (const juce::var& entry)
{
    const auto* array = entry.getArray();
    return array != nullptr && array->size() > 2 && (int) array->getReference (2) != 0;
}

bool TouchLiveBrowserView::entryIsLoadable (const juce::var& entry)
{
    const auto* array = entry.getArray();
    return array != nullptr && array->size() > 3 && (int) array->getReference (3) != 0;
}

//==============================================================================
void TouchLiveBrowserView::resized()
{
    auto area = getLocalBounds().reduced (4);

    auto header = area.removeFromTop (headerHeight).reduced (0, 3);
    backTile.setBounds (header.removeFromLeft (44));

    auto footer = area.removeFromBottom (footerHeight).reduced (0, 3);
    previewTile.setBounds (footer.removeFromLeft (56));
    loadTile.setBounds (footer.removeFromRight (72));

    viewport.setBounds (area);
    refreshList();
}

void TouchLiveBrowserView::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::background);

    // Breadcrumb rechts vom Zurück-Chip
    juce::String crumb;
    const auto separator = juce::String::fromUTF8 ("  \xE2\x80\xBA  ");   // ›

    for (size_t i = 1; i < levels.size(); ++i)
        crumb << (crumb.isEmpty() ? juce::String() : separator) << levels[i].name;

    if (crumb.isEmpty())
        crumb = "Browser";

    g.setColour (push::colours::text);
    g.setFont (push::scaledFont (14.0f));
    g.drawFittedText (crumb,
                      getLocalBounds().reduced (4).removeFromTop (headerHeight)
                          .withTrimmedLeft (54).reduced (0, 3),
                      juce::Justification::centredLeft, 1, 1.0f);

    if (loading)
    {
        g.setColour (push::colours::textDim);
        g.setFont (push::scaledFont (13.0f));
        g.drawText (juce::String::fromUTF8 ("lädt …"), getLocalBounds(),
                    juce::Justification::centred);
    }
    else if (currentItems.isEmpty() && ! levels.empty())
    {
        g.setColour (push::colours::textDim);
        g.setFont (push::scaledFont (13.0f));
        g.drawText ("leer", getLocalBounds(), juce::Justification::centred);
    }
}

} // namespace conduit
