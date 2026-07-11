#include "TrackTabsStrip.h"

#include "PushLookAndFeel.h"
#include "TrackFocusBadge.h"

namespace conduit
{

TrackTabsStrip::TrackTabsStrip (LiveSetModel& modelToUse)
    : model (modelToUse)
{
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
    repaint();
}

int TrackTabsStrip::tabWidth() const noexcept
{
    if (rows.empty())
        return 0;

    return juce::jmin (kMaxTabWidth, getWidth() / (int) rows.size());
}

int TrackTabsStrip::tabIndexAt (int x) const noexcept
{
    const auto width = tabWidth();
    if (width <= 0)
        return -1;

    const auto index = x / width;
    return index >= 0 && index < (int) rows.size() ? index : -1;
}

void TrackTabsStrip::paint (juce::Graphics& g)
{
    const auto width = tabWidth();

    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto& row = rows[(size_t) i];
        const auto tab = juce::Rectangle<int> (i * width, 0, width, getHeight())
                             .reduced (2, 1).toFloat();

        const auto isFocus = row.key.isNotEmpty() && row.key == focusKey;

        // Push-Optik (User-Feedback 11.07.2026): Nummer + Name in Jost in
        // der Track-Farbe, der angewählte Track grau unterlegt — keine
        // farbigen Rahmen (Farbe kommt später übers Pad-MIDI-Feedback).
        if (isFocus)
        {
            g.setColour (push::colours::tileActive);
            g.fillRoundedRectangle (tab, 3.0f);
        }

        g.setColour (row.colour);
        g.setFont (push::scaledFont (12.0f));
        g.drawText (juce::String (row.index + 1) + " " + row.name,
                    tab.reduced (7.0f, 0.0f).toNearestInt(),
                    juce::Justification::centredLeft);
    }
}

void TrackTabsStrip::mouseUp (const juce::MouseEvent& event)
{
    if (! getLocalBounds().contains (event.getPosition()))
        return;

    const auto index = tabIndexAt (event.getPosition().x);
    if (index < 0)
        return;

    if (onTrackChosen != nullptr)
        onTrackChosen (rows[(size_t) index].key);
}

} // namespace conduit
