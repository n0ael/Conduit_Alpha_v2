#include "HardwareTargetPicker.h"

#include "PushLookAndFeel.h"

namespace conduit
{

//==============================================================================
HardwareTargetPicker::HardwareTargetPicker (MidiTargetBrowserModel modelToUse)
    : model (std::move (modelToUse))
{
    setSize (kPanelWidth, kPanelHeight);

    backTile.setTooltip (juce::String::fromUTF8 ("Zur\xc3\xbc" "ck"));
    backTile.onClick = [this] { model.goBack(); refresh(); };
    addAndMakeVisible (backTile);

    viewport.setViewedComponent (&listContent, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    searchField.setTextToShowWhenEmpty (juce::String::fromUTF8 ("Suchen\xe2\x80\xa6"),
                                        push::colours::textDim);
    searchField.onTextChange = [this] { model.setFilter (searchField.getText()); refresh(); };
    searchField.onReturnKey  = [this] { setKeyboardVisible (false); };
    searchField.onEscapeKey  = [this]
    {
        searchField.setText ({}, juce::dontSendNotification);
        model.setFilter ({});
        refresh();
        setKeyboardVisible (false);
    };
    addAndMakeVisible (searchField);

    // TouchKeyboard (Muster Browser/BrowserPanel): tippt NUR ins Suchfeld,
    // klappt beim Fokussieren auf -- Sichtbarkeit steuert der Fokus-Listener,
    // die Tasten selbst greifen nie den Fokus (Fokus-Falle, s. TouchKeyboard.h).
    keyboard.setTarget (&searchField);
    keyboard.onCloseRequested = [this] { setKeyboardVisible (false); };
    addChildComponent (keyboard);

    keyboardSlide.onUpdate = [this] (float) { resized(); };
    juce::Desktop::getInstance().addFocusChangeListener (this);

    refresh();
}

HardwareTargetPicker::~HardwareTargetPicker()
{
    juce::Desktop::getInstance().removeFocusChangeListener (this);
}

void HardwareTargetPicker::refresh()
{
    breadcrumb = model.breadcrumbText();
    backTile.setVisible (! model.isAtTop());
    listContent.setRows (model.currentRows());
    resized();
    repaint();
}

void HardwareTargetPicker::chooseRow (int rowIndex)
{
    const auto rows = model.currentRows();
    if (rowIndex < 0 || rowIndex >= (int) rows.size())
        return;

    const auto& row = rows[(size_t) rowIndex];
    if (row.kind == MidiTargetBrowserModel::Kind::parameter)
    {
        if (onTargetChosen != nullptr)
            onTargetChosen (row);

        // Muster TrackSelectorPanel::mouseUp: der Picker schliesst sich nach
        // einer Auswahl selbst -- der Aufrufer muss die CallOutBox nicht kennen.
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
        return;
    }

    // M9c: Preset-Zeile = Auswahl (wie parameter), Aktions-Zeile = Scan
    // starten -- der Picker bleibt offen und pollt den Fortschritt.
    if (row.kind == MidiTargetBrowserModel::Kind::preset)
    {
        if (onPresetChosen != nullptr)
            onPresetChosen (row);

        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
        return;
    }

    if (row.kind == MidiTargetBrowserModel::Kind::action)
    {
        if (onScanRequested != nullptr)
        {
            onScanRequested (row.deviceId);
            startTimerHz (4);   // Status-Polling (<= 10 Hz, Rule ui-design)
            refresh();
        }
        return;
    }

    model.enter (rowIndex);
    searchField.setText ({}, juce::dontSendNotification);
    refresh();
}

void HardwareTargetPicker::timerCallback()
{
    // Poll-Ende, sobald kein Scan mehr laeuft (Status leer) -- die letzte
    // refresh()-Runde zeigt dann die frisch gescannten Baenke.
    auto scanning = false;
    for (const auto& row : model.currentRows())
        if (row.kind == MidiTargetBrowserModel::Kind::action
            && model.scanStatusFor != nullptr
            && model.scanStatusFor (row.deviceId).isNotEmpty())
            scanning = true;

    if (! scanning)
        stopTimer();

    refresh();
}

void HardwareTargetPicker::setKeyboardVisible (bool shouldShow, bool animate)
{
    if (keyboardVisible == shouldShow)
        return;

    keyboardVisible = shouldShow;

    if (shouldShow)
        keyboard.setVisible (true);

    if (animate)
        keyboardSlide.animateTo (shouldShow ? 1.0f : 0.0f, kKeyboardAnimMs);
    else
        keyboardSlide.snapTo (shouldShow ? 1.0f : 0.0f);

    if (! shouldShow)
        keyboard.setVisible (false);
}

void HardwareTargetPicker::globalFocusChanged (juce::Component* focusedComponent)
{
    // Suchfeld fokussiert -> aufklappen; Fokus ausserhalb von Suchfeld +
    // Tastatur-Subtree -> einklappen (die Tasten selbst greifen nie den
    // Fokus, tauchen hier also gar nicht auf -- doppelte Absicherung,
    // Muster Browser/BrowserPanel::globalFocusChanged).
    if (focusedComponent == &searchField
        || (focusedComponent != nullptr && searchField.isParentOf (focusedComponent)))
    {
        setKeyboardVisible (true);
        return;
    }

    if (focusedComponent != nullptr
        && (focusedComponent == &keyboard || keyboard.isParentOf (focusedComponent)))
        return;   // Whitelist: Tastatur-Subtree schliesst nicht

    setKeyboardVisible (false);
}

void HardwareTargetPicker::paint (juce::Graphics& g)
{
    g.setColour (push::colours::background);
    g.fillAll();
    g.setColour (push::colours::outline);
    g.drawRect (getLocalBounds());

    auto header = getLocalBounds().removeFromTop (kHeaderHeight);
    g.setColour (push::colours::tile);
    g.fillRect (header);
    g.setColour (push::colours::outline);
    g.drawLine ((float) header.getX(), (float) header.getBottom(),
               (float) header.getRight(), (float) header.getBottom());

    auto breadcrumbArea = header;
    breadcrumbArea.removeFromLeft (kHeaderHeight);
    g.setColour (push::colours::text);
    g.setFont (push::scaledFont (13.0f));
    const auto title = breadcrumb.isNotEmpty() ? breadcrumb
                                               : juce::String::fromUTF8 ("Ger\xc3\xa4t w\xc3\xa4hlen");
    g.drawFittedText (title, breadcrumbArea.reduced (8, 0), juce::Justification::centredLeft, 1, 1.0f);
}

void HardwareTargetPicker::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (kHeaderHeight);
    backTile.setBounds (header.removeFromLeft (kHeaderHeight).reduced (4));

    auto bottom = area.removeFromBottom (kSearchHeight);
    searchField.setBounds (bottom.reduced (4, 6));

    const auto keyboardHeight = juce::roundToInt (keyboardSlide.getCurrent()
                                                  * (float) keyboard.preferredHeight());
    if (keyboardHeight > 0)
        keyboard.setBounds (area.removeFromBottom (keyboardHeight));

    viewport.setBounds (area);
    listContent.setSize (area.getWidth(), listContent.getHeight());
}

//==============================================================================
HardwareTargetPicker::RowListContent::RowListContent (HardwareTargetPicker& ownerToUse)
    : owner (ownerToUse)
{
}

void HardwareTargetPicker::RowListContent::setRows (std::vector<MidiTargetBrowserModel::Row> newRows)
{
    rows = std::move (newRows);
    setSize (getWidth() > 0 ? getWidth() : HardwareTargetPicker::kPanelWidth,
             (int) rows.size() * HardwareTargetPicker::kRowHeight);
    repaint();
}

void HardwareTargetPicker::RowListContent::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::panel);

    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto& row = rows[(size_t) i];
        const juce::Rectangle<int> bounds (0, i * HardwareTargetPicker::kRowHeight,
                                           getWidth(), HardwareTargetPicker::kRowHeight);

        g.setColour (push::colours::outline.withAlpha (0.3f));
        g.drawLine ((float) bounds.getX(), (float) bounds.getBottom(),
                   (float) bounds.getRight(), (float) bounds.getBottom());

        auto textArea = bounds.reduced (12, 0);
        textArea.removeFromLeft (row.indent * 16);

        // M9c: preset (Auswahl) und action (Scan) sind Blaetter wie parameter.
        const auto navigable = row.kind != MidiTargetBrowserModel::Kind::parameter
                               && row.kind != MidiTargetBrowserModel::Kind::preset
                               && row.kind != MidiTargetBrowserModel::Kind::action;
        if (navigable)
        {
            auto chevronArea = textArea.removeFromRight (20);
            g.setColour (push::colours::textDim);
            g.setFont (push::scaledFont (12.0f));
            g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb8"), chevronArea, juce::Justification::centred);
        }

        g.setColour (row.kind == MidiTargetBrowserModel::Kind::action
                         ? push::colours::ledOrange
                         : push::colours::text);
        g.setFont (push::scaledFont (13.0f));
        g.drawFittedText (row.label, textArea, juce::Justification::centredLeft, 1, 1.0f);
    }
}

void HardwareTargetPicker::RowListContent::mouseUp (const juce::MouseEvent& event)
{
    const auto index = rowIndexAt (event.getPosition());
    if (index >= 0)
        owner.chooseRow (index);
}

int HardwareTargetPicker::RowListContent::rowIndexAt (juce::Point<int> position) const noexcept
{
    if (position.y < 0 || rows.empty())
        return -1;

    const auto index = position.y / HardwareTargetPicker::kRowHeight;
    return index >= 0 && index < (int) rows.size() ? index : -1;
}

} // namespace conduit
