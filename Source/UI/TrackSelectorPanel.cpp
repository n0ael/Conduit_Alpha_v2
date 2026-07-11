#include "TrackSelectorPanel.h"

#include <algorithm>

#include "PushLookAndFeel.h"

namespace conduit
{

TrackSelectorPanel::TrackSelectorPanel (LiveSetModel& model, bool followEnabled)
    : rows (midiTrackRowsFrom (model)),
      focusKey (focusKeyFrom (model)),
      follow (followEnabled)
{
    const auto listRows = juce::jmax (1, (int) rows.size());   // 1 = Leerzeile
    setSize (kPanelWidth, kTitleHeight + kFollowHeight + listRows * kRowHeight);
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
                                               bool followSelection)
{
    return juce::OSCMessage ("/live/song/set/midi_input_focus",
                             trackKey, gridInputName, masterInputName,
                             (juce::int32) (followSelection ? 1 : 0));
}

juce::OSCMessage TrackSelectorPanel::makeFollowCommand (bool shouldFollow)
{
    return juce::OSCMessage ("/live/song/set/midi_input_follow",
                             (juce::int32) (shouldFollow ? 1 : 0));
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

    // Follow-Selection-Zeile (Block H v2): LED-Punkt + Beschriftung
    auto followArea = area.removeFromTop (kFollowHeight);
    if (followHovered)
    {
        g.setColour (push::colours::tileActive);
        g.fillRect (followArea);
    }

    const auto led = followArea.reduced (12, 15).removeFromLeft (14).toFloat();
    g.setColour (follow ? push::colours::ledCyan
                        : push::colours::textDim.withAlpha (0.4f));
    g.fillEllipse (led.withSizeKeepingCentre (12.0f, 12.0f));

    g.setColour (follow ? push::colours::text : push::colours::textDim);
    g.setFont (push::scaledFont (15.0f));
    g.drawText ("Follow Selection", followArea.reduced (12, 0).withTrimmedLeft (26),
                juce::Justification::centredLeft);

    g.setColour (push::colours::textDim.withAlpha (0.3f));
    g.drawHorizontalLine (followArea.getBottom() - 1,
                          (float) followArea.getX() + 8.0f,
                          (float) followArea.getRight() - 8.0f);

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
    const auto listTop = kTitleHeight + kFollowHeight;
    const auto index = (position.y - listTop) / kRowHeight;
    return position.y >= listTop && index < (int) rows.size() ? index : -1;
}

bool TrackSelectorPanel::isInFollowRow (juce::Point<int> position) const noexcept
{
    return position.y >= kTitleHeight && position.y < kTitleHeight + kFollowHeight;
}

void TrackSelectorPanel::mouseMove (const juce::MouseEvent& event)
{
    const auto row = rowIndexAt (event.getPosition());
    const auto overFollow = isInFollowRow (event.getPosition());

    if (row == hoveredRow && overFollow == followHovered)
        return;

    hoveredRow = row;
    followHovered = overFollow;
    repaint();
}

void TrackSelectorPanel::mouseExit (const juce::MouseEvent&)
{
    if (hoveredRow < 0 && ! followHovered)
        return;

    hoveredRow = -1;
    followHovered = false;
    repaint();
}

void TrackSelectorPanel::mouseUp (const juce::MouseEvent& event)
{
    if (isInFollowRow (event.getPosition()))
    {
        follow = ! follow;
        repaint();

        if (onFollowChanged != nullptr)
            onFollowChanged (follow);
        return;   // Panel bleibt offen (weiter Track wählbar)
    }

    const auto row = rowIndexAt (event.getPosition());
    if (row < 0)
        return;

    if (onTrackChosen != nullptr)
        onTrackChosen (rows[(size_t) row].key);

    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

} // namespace conduit
