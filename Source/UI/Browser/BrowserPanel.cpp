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
BrowserPanel::BrowserPanel (BrowserModel& modelToUse, UiSettings& uiSettingsToUse)
    : model (modelToUse), uiSettings (uiSettingsToUse)
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
    searchField.onReturnKey  = [this]
    {
        timerCallback();
        setKeyboardVisible (false);   // Commit klappt die Tastatur ein
    };
    searchField.onEscapeKey  = [this]
    {
        searchField.setText ({}, juce::dontSendNotification);
        timerCallback();
        setKeyboardVisible (false);
    };
    addAndMakeVisible (searchField);

    // TouchKeyboard (M5): tippt NUR ins Suchfeld, klappt beim Fokussieren
    // auf (Setting-abhängig) — Sichtbarkeit steuert der Fokus-Listener
    keyboard.setTarget (&searchField);
    keyboard.onCloseRequested = [this] { setKeyboardVisible (false); };
    addChildComponent (keyboard);

    keyboardSlide.onUpdate = [this] (float) { resized(); };
    juce::Desktop::getInstance().addFocusChangeListener (this);

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
    juce::Desktop::getInstance().removeFocusChangeListener (this);
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

    if (! open)
        setKeyboardVisible (false, false);   // Panel zu → Tastatur sofort weg

    if (animate)
        slide.animateTo (open ? 1.0f : 0.0f, slideMs);
    else
        slide.snapTo (open ? 1.0f : 0.0f);
}

//==============================================================================
void BrowserPanel::setKeyboardVisible (bool shouldShow, bool animate)
{
    if (shouldShow && ! uiSettings.isSoftKeyboardEnabled())
        return;   // Setting aus → Suchfeld verhält sich wie ein normales Feld

    if (keyboardVisible == shouldShow)
        return;

    keyboardVisible = shouldShow;
    model.state.setProperty ("softKeyboardVisible", shouldShow, nullptr);

    if (shouldShow)
        keyboard.setVisible (true);

    if (animate)
        keyboardSlide.animateTo (shouldShow ? 1.0f : 0.0f, slideMs);
    else
        keyboardSlide.snapTo (shouldShow ? 1.0f : 0.0f);
}

void BrowserPanel::refreshSoftKeyboardSetting()
{
    if (! uiSettings.isSoftKeyboardEnabled())
        setKeyboardVisible (false);
}

void BrowserPanel::globalFocusChanged (juce::Component* focusedComponent)
{
    // Suchfeld fokussiert → aufklappen; Fokus außerhalb von Suchfeld +
    // Tastatur-Subtree → einklappen (die Tasten selbst greifen nie den
    // Fokus, tauchen hier also gar nicht auf — doppelte Absicherung)
    if (focusedComponent == &searchField
        || (focusedComponent != nullptr && searchField.isParentOf (focusedComponent)))
    {
        setKeyboardVisible (true);
        return;
    }

    if (focusedComponent != nullptr
        && (focusedComponent == &keyboard || keyboard.isParentOf (focusedComponent)))
        return;   // Whitelist: Tastatur-Subtree schließt nicht

    if (focusedComponent != nullptr)
        setKeyboardVisible (false);
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

    // Oberkante der Suchzeile + Lupe (Zeile wandert mit dem Keyboard hoch)
    g.fillRect (0, searchField.getY() - 7, getWidth(), 1);
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

    // TouchKeyboard schiebt sich von unten ein (Suchfeld rutscht hoch)
    const auto keyboardFull    = keyboard.preferredHeight();
    const auto keyboardVisiblePx = juce::roundToInt (
        keyboardSlide.getCurrent() * (float) keyboardFull);
    keyboard.setBounds (bounds.getX(), getHeight() - keyboardVisiblePx,
                        bounds.getWidth(), keyboardFull);
    keyboard.setVisible (keyboardVisiblePx > 0);
    bounds.removeFromBottom (keyboardVisiblePx);

    // Suchzeile darüber: Lupe links, Feld daneben
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
