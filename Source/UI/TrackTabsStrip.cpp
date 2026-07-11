#include "TrackTabsStrip.h"

#include "PushLookAndFeel.h"

namespace conduit
{

TrackTabsStrip::TrackTabsStrip (LiveSetModel& modelToUse,
                                GridPanelSettings& panelSettingsToUse)
    : model (modelToUse), panelSettings (panelSettingsToUse)
{
    fontPx = panelSettings.getTrackTabsFontPx();
    minTabWidthPx = panelSettings.getTrackTabMinWidthPx();
    refresh();
}

void TrackTabsStrip::refresh()
{
    auto newRows = TrackSelectorPanel::midiTrackRowsFrom (model);
    auto newFocus = TrackSelectorPanel::focusKeyFrom (model);

    const auto sameRows = newRows.size() == rows.size()
        && std::equal (newRows.begin(), newRows.end(), rows.begin(),
                       [] (const auto& a, const auto& b)
                       {
                           return a.key == b.key && a.name == b.name
                                  && a.colour == b.colour;
                       });

    if (sameRows && newFocus == focusKey)
        return;

    rows = std::move (newRows);
    focusKey = std::move (newFocus);
    clampScroll();
    repaint();
}

void TrackTabsStrip::setFontPx (int newFontPx)
{
    if (newFontPx == fontPx)
        return;

    fontPx = newFontPx;
    repaint();
}

void TrackTabsStrip::setMinTabWidth (int newMinWidthPx)
{
    if (newMinWidthPx == minTabWidthPx)
        return;

    minTabWidthPx = newMinWidthPx;
    clampScroll();
    repaint();
}

//==============================================================================
int TrackTabsStrip::tabWidth() const noexcept
{
    if (rows.empty())
        return 0;

    // Gleichverteilung, aber nie schmaler als die Mindestbreite (dann
    // scrollt der Strip) und nie breiter als kMaxTabWidth.
    const auto even = getWidth() / (int) rows.size();
    return juce::jlimit (minTabWidthPx, kMaxTabWidth, even);
}

int TrackTabsStrip::contentWidth() const noexcept
{
    return tabWidth() * (int) rows.size();
}

void TrackTabsStrip::clampScroll()
{
    scrollOffsetPx = juce::jlimit (0, juce::jmax (0, contentWidth() - getWidth()),
                                   scrollOffsetPx);
}

int TrackTabsStrip::tabIndexAt (int x) const noexcept
{
    const auto width = tabWidth();
    if (width <= 0)
        return -1;

    const auto index = (x + scrollOffsetPx) / width;
    return index >= 0 && index < (int) rows.size() ? index : -1;
}

//==============================================================================
void TrackTabsStrip::beginPress (int x)
{
    pressActive = true;
    scrolling = false;
    pressedTab = tabIndexAt (x);
    scrollStartPx = scrollOffsetPx;

    if (pressedTab >= 0)
        startTimer (kSelectHoldMs);
}

void TrackTabsStrip::movePress (int totalDeltaX)
{
    if (! pressActive)
        return;

    // Horizontales Ziehen = Scrollen; bricht die Halte-Auswahl ab
    // (Verzögerungs-Schutz, User-Wunsch Runde 3).
    if (! scrolling && std::abs (totalDeltaX) > kScrollTolerancePx)
    {
        scrolling = true;
        stopTimer();
    }

    if (scrolling)
    {
        scrollOffsetPx = scrollStartPx - totalDeltaX;
        clampScroll();
        repaint();
    }
}

void TrackTabsStrip::fireSelectTimeout()
{
    stopTimer();

    if (! pressActive || scrolling || pressedTab < 0
        || pressedTab >= (int) rows.size())
        return;

    if (onTrackChosen != nullptr)
        onTrackChosen (rows[(size_t) pressedTab].key);
}

void TrackTabsStrip::endPress()
{
    stopTimer();
    pressActive = false;
    scrolling = false;
    pressedTab = -1;
}

//==============================================================================
void TrackTabsStrip::paint (juce::Graphics& g)
{
    const auto width = tabWidth();

    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto& row = rows[(size_t) i];
        const auto tab = juce::Rectangle<int> (i * width - scrollOffsetPx, 0,
                                               width, getHeight())
                             .reduced (2, 1).toFloat();

        if (! tab.intersects (getLocalBounds().toFloat()))
            continue;

        const auto isFocus = row.key.isNotEmpty() && row.key == focusKey;

        // Push-Optik: Nummer + Name in Jost in der Track-Farbe, der
        // angewählte Track grau unterlegt — keine farbigen Rahmen.
        if (isFocus)
        {
            g.setColour (push::colours::tileActive);
            g.fillRoundedRectangle (tab, 3.0f);
        }

        g.setColour (row.colour);
        g.setFont (push::scaledFont ((float) fontPx));
        g.drawText (juce::String (row.index + 1) + " " + row.name,
                    tab.reduced (7.0f, 0.0f).toNearestInt(),
                    juce::Justification::centredLeft);
    }
}

void TrackTabsStrip::mouseDown (const juce::MouseEvent& event)
{
    beginPress (event.getPosition().x);
}

void TrackTabsStrip::mouseDrag (const juce::MouseEvent& event)
{
    movePress (event.getDistanceFromDragStartX());
}

void TrackTabsStrip::mouseUp (const juce::MouseEvent&)
{
    endPress();
}

} // namespace conduit
