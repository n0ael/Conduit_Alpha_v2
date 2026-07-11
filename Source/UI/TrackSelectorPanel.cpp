#include "TrackSelectorPanel.h"

#include <algorithm>

#include "PushLookAndFeel.h"

namespace conduit
{

TrackSelectorPanel::TrackSelectorPanel (LiveSetModel& model)
    : rows (midiTrackRowsFrom (model)),
      focusKey (focusKeyFrom (model))
{
    const auto listRows = juce::jmax (1, (int) rows.size());   // 1 = Leerzeile
    setSize (kPanelWidth, kTitleHeight + listRows * kRowHeight);
}

//==============================================================================
std::vector<TrackSelectorPanel::TrackRow>
TrackSelectorPanel::midiTrackRowsFrom (LiveSetModel& model)
{
    std::vector<TrackRow> result;

    auto domain = model.getDomain ("tracks");

    for (const auto& item : domain)
    {
        if (! item.hasType (touchlive::id::item))
            continue;

        if (item.getProperty ("kind").toString() != "midi")
            continue;

        TrackRow row;
        row.key    = item.getProperty (touchlive::id::itemKey).toString();
        row.name   = item.getProperty ("name").toString();
        row.colour = juce::Colour (0xff000000u
                                   | (juce::uint32) (int) item.getProperty ("color", 0));
        row.index  = (int) item.getProperty ("index", 0);
        result.push_back (std::move (row));
    }

    std::sort (result.begin(), result.end(),
               [] (const TrackRow& a, const TrackRow& b) { return a.index < b.index; });

    return result;
}

juce::String TrackSelectorPanel::focusKeyFrom (LiveSetModel& model)
{
    return model.getDomain ("tracks").getProperty ("conduit_focus").toString();
}

juce::OSCMessage
TrackSelectorPanel::makeMidiInputFocusCommand (const juce::String& trackKey,
                                               const juce::String& gridInputName,
                                               const juce::String& masterInputName,
                                               const juce::String& favouritesJoined)
{
    return juce::OSCMessage ("/live/song/set/midi_input_focus",
                             trackKey, gridInputName, masterInputName,
                             favouritesJoined);
}

//==============================================================================
void TrackSelectorPanel::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::background);

    auto area = getLocalBounds();

    g.setColour (push::colours::textDim);
    g.setFont (push::scaledFont (13.0f));
    g.drawText ("Ableton-Track spielen", area.removeFromTop (kTitleHeight).reduced (12, 0),
                juce::Justification::centredLeft);

    if (rows.empty())
    {
        g.setColour (push::colours::textDim.withAlpha (0.7f));
        g.setFont (push::scaledFont (14.0f));
        g.drawText ("Keine MIDI-Tracks (Ableton verbunden?)",
                    area.removeFromTop (kRowHeight).reduced (12, 0),
                    juce::Justification::centredLeft);
        return;
    }

    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto& row = rows[(size_t) i];
        auto rowArea = area.removeFromTop (kRowHeight);

        const auto isFocus = row.key.isNotEmpty() && row.key == focusKey;

        if (isFocus)
        {
            g.setColour (row.colour.withAlpha (0.25f));
            g.fillRect (rowArea);
        }
        else if (i == hoveredRow)
        {
            g.setColour (push::colours::tileActive);
            g.fillRect (rowArea);
        }

        // Farb-Swatch in Live-Farbe links, Name daneben (nie stauchen)
        const auto swatch = rowArea.reduced (12, 12).removeFromLeft (20).toFloat();
        g.setColour (row.colour);
        g.fillRoundedRectangle (swatch, 3.0f);

        g.setColour (push::colours::text);
        g.setFont (push::scaledFont (15.0f));
        g.drawText (row.name, rowArea.reduced (12, 0).withTrimmedLeft (32),
                    juce::Justification::centredLeft);
    }
}

int TrackSelectorPanel::rowIndexAt (juce::Point<int> position) const noexcept
{
    const auto index = (position.y - kTitleHeight) / kRowHeight;
    return position.y >= kTitleHeight && index < (int) rows.size() ? index : -1;
}

void TrackSelectorPanel::mouseMove (const juce::MouseEvent& event)
{
    const auto row = rowIndexAt (event.getPosition());
    if (row == hoveredRow)
        return;

    hoveredRow = row;
    repaint();
}

void TrackSelectorPanel::mouseExit (const juce::MouseEvent&)
{
    if (hoveredRow < 0)
        return;

    hoveredRow = -1;
    repaint();
}

void TrackSelectorPanel::mouseUp (const juce::MouseEvent& event)
{
    const auto row = rowIndexAt (event.getPosition());
    if (row < 0)
        return;

    if (onTrackChosen != nullptr)
        onTrackChosen (rows[(size_t) row].key);

    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

} // namespace conduit
