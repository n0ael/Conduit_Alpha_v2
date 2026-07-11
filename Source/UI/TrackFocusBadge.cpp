#include "TrackFocusBadge.h"

#include "PushLookAndFeel.h"

namespace conduit
{

TrackFocusBadge::FocusRow TrackFocusBadge::focusRowFrom (LiveSetModel& model)
{
    FocusRow row;

    const auto key = model.getDomain ("tracks").getProperty ("conduit_focus").toString();
    if (key.isEmpty())
        return row;

    auto item = model.findItem ("tracks", key);
    if (! item.isValid())
        return row;

    row.key    = key;
    row.name   = item.getProperty ("name").toString();
    row.colour = juce::Colour (0xff000000u
                               | (juce::uint32) (int) item.getProperty ("color", 0));
    return row;
}

void TrackFocusBadge::setFocus (const FocusRow& newFocus)
{
    if (focus.key == newFocus.key && focus.name == newFocus.name
        && focus.colour == newFocus.colour)
        return;

    focus = newFocus;
    repaint();
}

void TrackFocusBadge::paint (juce::Graphics& g)
{
    if (! hasFocus())
        return;

    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (push::colours::tile);
    g.fillRoundedRectangle (bounds, 4.0f);

    auto area = getLocalBounds().reduced (6, 4);
    const auto swatch = area.removeFromLeft (14).toFloat()
                            .withSizeKeepingCentre (12.0f, 12.0f);
    g.setColour (focus.colour);
    g.fillRoundedRectangle (swatch, 3.0f);

    g.setColour (push::colours::text);
    g.setFont (push::scaledFont (13.0f));
    g.drawText (focus.name, area.withTrimmedLeft (6),
                juce::Justification::centredLeft);
}

} // namespace conduit
