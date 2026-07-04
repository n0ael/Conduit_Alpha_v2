#include "BrowserPanel.h"

#include "UI/Browser/BrowserListRow.h"
#include "UI/PushIcons.h"
#include "UI/PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr int slideMs = 180;
} // namespace

//==============================================================================
BrowserPanel::BrowserPanel (BrowserModel& modelToUse)
    : model (modelToUse)
{
    backTile.setTooltip (juce::String::fromUTF8 ("Zurück"));
    backTile.onClick = [this] { model.goBack(); };
    addChildComponent (backTile);   // sichtbar nur wenn canGoBack()

    breadcrumbLabel.setColour (juce::Label::textColourId, push::colours::text);
    breadcrumbLabel.setJustificationType (juce::Justification::centredLeft);
    breadcrumbLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (breadcrumbLabel);

    list.setModel (this);
    list.setRowHeight (rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, push::colours::panel);
    list.getViewport()->setScrollBarsShown (true, false);
    // Touch-Flick-Scrolling: Drag auf den Zeilen scrollt die Liste
    list.getViewport()->setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    addAndMakeVisible (list);

    // Suchfeld ganz unten (Daumen-Erreichbarkeit) — Live-Filter mit
    // Debounce; Escape löscht, Return committet sofort
    searchField.setTextToShowWhenEmpty ("Suchen", push::colours::textDim);
    searchField.setColour (juce::TextEditor::backgroundColourId, push::colours::tile);
    searchField.setColour (juce::TextEditor::outlineColourId, push::colours::outline);
    searchField.setColour (juce::TextEditor::focusedOutlineColourId, push::colours::textDim);
    searchField.setColour (juce::TextEditor::textColourId, push::colours::text);
    searchField.onTextChange = [this] { startTimer (searchDebounceMs); };
    searchField.onReturnKey  = [this] { timerCallback(); };
    searchField.onEscapeKey  = [this]
    {
        searchField.setText ({}, juce::dontSendNotification);
        timerCallback();
    };
    addAndMakeVisible (searchField);

    model.onRowsChanged = [this] { refreshFromModel(); };

    slide.onUpdate = [this] (float value)
    {
        setVisible (value > 0.0f);

        if (onDockWidthChanged != nullptr)
            onDockWidthChanged();
    };

    setVisible (false);
    updateHeader();
}

BrowserPanel::~BrowserPanel()
{
    model.onRowsChanged = nullptr;
    list.setModel (nullptr);
}

//==============================================================================
void BrowserPanel::setOpen (bool shouldBeOpen, bool animate)
{
    if (open == shouldBeOpen)
        return;

    open = shouldBeOpen;

    if (open)
    {
        model.openStartSection();   // Startbereich der aktiven Page
        setVisible (true);
    }

    if (animate)
        slide.animateTo (open ? 1.0f : 0.0f, slideMs);
    else
        slide.snapTo (open ? 1.0f : 0.0f);
}

int BrowserPanel::currentDockWidth() const
{
    return juce::roundToInt (slide.getCurrent() * (float) dockWidth);
}

//==============================================================================
void BrowserPanel::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::panel);

    // Trennkante zum Canvas + Unterkante des Headers
    g.setColour (push::colours::outline);
    g.fillRect (0, 0, 1, getHeight());
    g.fillRect (0, headerHeight - 1, getWidth(), 1);

    // Oberkante der Suchzeile + Lupe
    g.fillRect (0, getHeight() - searchHeight, getWidth(), 1);
    if (! searchIconArea.isEmpty())
        push::draw (g, push::Icon::search,
                    searchIconArea.withSizeKeepingCentre (20, 20).toFloat(),
                    push::colours::textDim);
}

void BrowserPanel::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromLeft (1);   // Trennkante

    auto header = bounds.removeFromTop (headerHeight);
    backTile.setBounds (header.removeFromLeft (headerHeight).reduced (4));
    breadcrumbLabel.setBounds (header.reduced (6, 0));

    // Suchzeile ganz unten: Lupe links, Feld daneben
    auto searchRow = bounds.removeFromBottom (searchHeight).reduced (8, 6);
    searchIconArea = searchRow.removeFromLeft (28);
    searchField.setBounds (searchRow);
    searchField.applyFontToAllText (push::scaledFont (15.0f));

    list.setBounds (bounds);
}

//==============================================================================
int BrowserPanel::getNumRows()
{
    return (int) model.rows().size();
}

juce::Component* BrowserPanel::refreshComponentForRow (int rowNumber, bool isRowSelected,
                                                       juce::Component* existingComponentToUpdate)
{
    const auto& rows = model.rows();

    if (rowNumber < 0 || rowNumber >= (int) rows.size())
    {
        delete existingComponentToUpdate;
        return nullptr;
    }

    auto* row = dynamic_cast<BrowserListRow*> (existingComponentToUpdate);

    if (row == nullptr)
    {
        delete existingComponentToUpdate;
        row = new BrowserListRow();
        row->onActivated = [this] (int index) { handleRowActivated (index); };
    }

    row->update (rows[(size_t) rowNumber], rowNumber, isRowSelected);
    return row;
}

void BrowserPanel::timerCallback()
{
    stopTimer();
    model.setSearchText (searchField.getText());
}

void BrowserPanel::handleRowActivated (int rowIndex)
{
    // Navigations-Zeilen konsumiert das Modell (Liste baut sich neu auf)
    if (model.activateRow (rowIndex))
        return;

    const auto& rows = model.rows();
    if (rowIndex < 0 || rowIndex >= (int) rows.size())
        return;

    const auto& row = rows[(size_t) rowIndex];

    if (row.kind == BrowserModel::Row::Kind::module)
    {
        list.selectRow (rowIndex);

        if (onModuleActivated != nullptr)
        {
            // Zeilen-Anker für Dialoge (Link-Send); Fallback: Panel-Bounds
            auto anchor = getScreenBounds();
            if (auto* rowComponent = list.getComponentForRowNumber (rowIndex))
                anchor = rowComponent->getScreenBounds();

            onModuleActivated (row.id, anchor);
        }
        return;
    }

    if (row.kind == BrowserModel::Row::Kind::action && onAction != nullptr)
        onAction (row.id);
}

//==============================================================================
void BrowserPanel::refreshFromModel()
{
    // Modell kann die Suche selbst löschen (goBack) — Feld nachziehen,
    // ohne den Debounce erneut anzustoßen
    if (searchField.getText().trim() != model.getSearchText())
        searchField.setText (model.getSearchText(), juce::dontSendNotification);

    list.deselectAllRows();
    list.updateContent();
    updateHeader();
    repaint();
}

void BrowserPanel::updateHeader()
{
    backTile.setVisible (model.canGoBack());
    breadcrumbLabel.setText (model.breadcrumbText(), juce::dontSendNotification);
    breadcrumbLabel.setFont (push::scaledFont (15.0f, true));
}

} // namespace conduit
