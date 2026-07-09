#include "TouchLiveGridView.h"

#include <algorithm>

#include "UI/PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr int panThreshold = 8;   // px — darunter zählt die Geste als Tap

    [[nodiscard]] juce::Colour colourFromLive (const juce::var& value)
    {
        return juce::Colour (0xff000000u | (juce::uint32) (juce::int64) value);
    }

    [[nodiscard]] juce::String slotState (const juce::var& slot)
    {
        if (auto* object = slot.getDynamicObject())
            return object->getProperty ("state").toString();

        return {};
    }
}

//==============================================================================
TouchLiveGridView::TouchLiveGridView (TouchLiveClient& clientToUse, LiveSetModel& modelToUse,
                                      TouchLiveSettings& settingsToUse)
    : client (clientToUse),
      model (modelToUse),
      settings (settingsToUse),
      modelState (model.getState()),
      vblank (this, [this] (double)
      {
          // Blink nur, wenn queued-Clips sichtbar sind — sonst kostenlos
          const auto phase = (juce::Time::getMillisecondCounter() / 350u) % 2u == 0u;

          if (phase != blinkPhase)
          {
              blinkPhase = phase;

              if (anyTriggered)
                  repaint();
          }
      })
{
    modelState.addListener (this);
    settings.addChangeListener (this);
    rebuildCache();
}

TouchLiveGridView::~TouchLiveGridView()
{
    settings.removeChangeListener (this);
    modelState.removeListener (this);
    cancelPendingUpdate();
}

//==============================================================================
void TouchLiveGridView::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    // Grob-Körnung reicht dem Grid: jede session-/tracks-Änderung landet
    // im coalesced Cache-Rebuild (Zellen sind diskrete Zustände)
    triggerAsyncUpdate();
}

void TouchLiveGridView::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)
{
    triggerAsyncUpdate();
}

void TouchLiveGridView::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)
{
    triggerAsyncUpdate();
}

void TouchLiveGridView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    clampScroll();
    repaint();   // Spaltenbreite geändert
}

void TouchLiveGridView::handleAsyncUpdate()
{
    rebuildCache();
}

void TouchLiveGridView::flushPendingRebuild()
{
    handleUpdateNowIfNeeded();
}

//==============================================================================
void TouchLiveGridView::rebuildCache()
{
    columns.clear();
    scenes.clear();
    anyTriggered = false;

    auto tracksDomain  = model.getDomain ("tracks");
    auto sessionDomain = model.getDomain ("session");

    // Szenen: Items "scene:{sid}" nach Live-Index
    struct SceneEntry { int index = 0; Scene scene; };
    std::vector<SceneEntry> sceneEntries;

    for (int i = 0; i < sessionDomain.getNumChildren(); ++i)
    {
        const auto item = sessionDomain.getChild (i);
        const auto key = item.getProperty (touchlive::id::itemKey).toString();

        if (! key.startsWith ("scene:"))
            continue;

        SceneEntry entry;
        entry.index = (int) item.getProperty ("index", 0);
        entry.scene.name = item.getProperty ("name").toString();
        entry.scene.colour = item.hasProperty ("color")
                                 ? colourFromLive (item.getProperty ("color"))
                                 : push::colours::tile;
        sceneEntries.push_back (std::move (entry));
    }

    std::sort (sceneEntries.begin(), sceneEntries.end(),
               [] (const SceneEntry& a, const SceneEntry& b) { return a.index < b.index; });

    for (auto& entry : sceneEntries)
        scenes.push_back (std::move (entry.scene));

    // Spalten: reguläre Tracks nach Live-Index; Grid-Zeile aus der
    // Domain-Property "grid:{tid}" (Array in Scene-Reihenfolge)
    struct ColumnEntry { int index = 0; Column column; };
    std::vector<ColumnEntry> columnEntries;

    for (int i = 0; i < tracksDomain.getNumChildren(); ++i)
    {
        const auto item = tracksDomain.getChild (i);
        const auto kind = item.getProperty ("kind").toString();

        if (kind != "audio" && kind != "midi")
            continue;

        ColumnEntry entry;
        entry.index = (int) item.getProperty ("index", 0);
        entry.column.key = item.getProperty (touchlive::id::itemKey).toString();
        entry.column.name = item.getProperty ("name").toString();
        entry.column.colour = item.hasProperty ("color")
                                  ? colourFromLive (item.getProperty ("color"))
                                  : push::colours::tile;

        const auto row = sessionDomain.getProperty ("grid:" + entry.column.key);

        if (const auto* slots = row.getArray())
            entry.column.slots = *slots;

        for (const auto& slot : entry.column.slots)
            anyTriggered = anyTriggered || slotState (slot) == "triggered";

        columnEntries.push_back (std::move (entry));
    }

    std::sort (columnEntries.begin(), columnEntries.end(),
               [] (const ColumnEntry& a, const ColumnEntry& b) { return a.index < b.index; });

    for (auto& entry : columnEntries)
        columns.push_back (std::move (entry.column));

    clampScroll();
    repaint();
}

//==============================================================================
int TouchLiveGridView::columnWidth() const
{
    return settings.getChannelWidth();
}

juce::Rectangle<int> TouchLiveGridView::gridArea() const
{
    return getLocalBounds()
        .withTrimmedTop (headerHeight)
        .withTrimmedRight (sceneColumn)
        .withTrimmedBottom (stopRowHeight);
}

void TouchLiveGridView::clampScroll()
{
    const auto area = gridArea();
    const auto maxX = juce::jmax (0, (int) columns.size() * columnWidth() - area.getWidth());
    const auto maxY = juce::jmax (0, (int) scenes.size() * cellHeight - area.getHeight());

    scrollX = juce::jlimit (0, maxX, scrollX);
    scrollY = juce::jlimit (0, maxY, scrollY);
}

void TouchLiveGridView::resized()
{
    clampScroll();
}

//==============================================================================
void TouchLiveGridView::paintCell (juce::Graphics& g, const juce::Rectangle<float>& cell,
                                   const juce::var& slot) const
{
    const auto inner = cell.reduced (1.5f);

    auto* object = slot.getDynamicObject();

    if (object == nullptr)
    {
        // Leerer Slot
        g.setColour (juce::Colour (0xff181818));
        g.fillRoundedRectangle (inner, 3.0f);
        return;
    }

    const auto state = object->getProperty ("state").toString();
    const auto clipColour = object->hasProperty ("color")
                                ? colourFromLive (object->getProperty ("color"))
                                : push::colours::tile;
    const auto name = object->getProperty ("name").toString();

    const auto recording = state == "recording";
    const auto playing   = state == "playing";
    const auto triggered = state == "triggered";

    auto fill = clipColour.withMultipliedBrightness (playing ? 1.0f : 0.75f);

    if (recording)
        fill = push::colours::ledRed.withMultipliedBrightness (0.8f);

    g.setColour (fill);
    g.fillRoundedRectangle (inner, 3.0f);

    // Zustands-Symbol links: ▷ (gestoppt/spielend/queued-blinkend) bzw. ●
    const auto symbolArea = juce::Rectangle<float> (inner.getX() + 4.0f,
                                                    inner.getCentreY() - 6.0f, 12.0f, 12.0f);
    const auto onDark = fill.getPerceivedBrightness() <= 0.55f;
    const auto ink = onDark ? juce::Colours::white : juce::Colours::black;

    if (recording)
    {
        g.setColour (blinkPhase ? juce::Colours::white : ink);
        g.fillEllipse (symbolArea.reduced (1.0f));
    }
    else
    {
        juce::Path triangle;
        triangle.addTriangle (symbolArea.getX(), symbolArea.getY(),
                              symbolArea.getX(), symbolArea.getBottom(),
                              symbolArea.getRight(), symbolArea.getCentreY());

        auto symbolColour = playing ? (onDark ? push::colours::ledGreen.brighter (0.2f)
                                              : push::colours::ledGreen.darker (0.3f))
                                    : ink.withAlpha (0.8f);

        // queued: blinkend (VBlank-Phase; Link-Beat-Kopplung = M2)
        if (triggered)
            symbolColour = blinkPhase ? juce::Colours::white : ink.withAlpha (0.35f);

        g.setColour (symbolColour);
        g.fillPath (triangle);
    }

    if (playing || (triggered && blinkPhase))
    {
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawRoundedRectangle (inner, 3.0f, 1.4f);
    }

    g.setColour (ink);
    g.setFont (push::scaledFont (11.0f));
    g.drawFittedText (name, inner.toNearestInt().withTrimmedLeft (20).reduced (2, 0),
                      juce::Justification::centredLeft, 1, 1.0f);
}

void TouchLiveGridView::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::background);

    const auto area = gridArea();
    const auto colW = columnWidth();

    if (columns.empty())
    {
        g.setColour (push::colours::textDim);
        g.setFont (push::scaledFont (15.0f));
        g.drawText (juce::String::fromUTF8 ("Keine Live-Verbindung — LIVE einschalten "
                                            "und ConduitRemote in Ableton wählen"),
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Track-Header (oben, angepinnt; scrollt horizontal mit)
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (0, 0, area.getWidth(), headerHeight);

        for (int c = 0; c < (int) columns.size(); ++c)
        {
            const auto x = c * colW - scrollX;

            if (x + colW < 0 || x > area.getWidth())
                continue;

            const auto& column = columns[(size_t) c];
            const auto header = juce::Rectangle<float> ((float) x + 1.0f, 1.0f,
                                                        (float) colW - 2.0f,
                                                        (float) headerHeight - 3.0f);
            g.setColour (column.colour);
            g.fillRoundedRectangle (header, 3.0f);

            g.setColour (column.colour.getPerceivedBrightness() > 0.55f
                             ? juce::Colours::black : juce::Colours::white);
            g.setFont (push::scaledFont (11.0f));
            g.drawFittedText (column.name, header.toNearestInt().reduced (4, 0),
                              juce::Justification::centred, 1, 1.0f);
        }
    }

    // Clip-Zellen (Mitte, beide Achsen scrollen)
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area);

        for (int c = 0; c < (int) columns.size(); ++c)
        {
            const auto x = area.getX() + c * colW - scrollX;

            if (x + colW < area.getX() || x > area.getRight())
                continue;

            const auto& column = columns[(size_t) c];

            for (int s = 0; s < (int) scenes.size(); ++s)
            {
                const auto y = area.getY() + s * cellHeight - scrollY;

                if (y + cellHeight < area.getY() || y > area.getBottom())
                    continue;

                const auto slot = s < column.slots.size() ? column.slots[s] : juce::var();
                paintCell (g, juce::Rectangle<float> ((float) x, (float) y,
                                                      (float) colW, (float) cellHeight),
                           slot);
            }
        }
    }

    // Scene-Fire-Spalte (rechts, angepinnt; scrollt vertikal mit)
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (getWidth() - sceneColumn, area.getY(), sceneColumn, area.getHeight());

        for (int s = 0; s < (int) scenes.size(); ++s)
        {
            const auto y = area.getY() + s * cellHeight - scrollY;

            if (y + cellHeight < area.getY() || y > area.getBottom())
                continue;

            const auto& scene = scenes[(size_t) s];
            const auto cell = juce::Rectangle<float> ((float) (getWidth() - sceneColumn) + 2.0f,
                                                      (float) y + 1.5f,
                                                      (float) sceneColumn - 4.0f,
                                                      (float) cellHeight - 3.0f);
            g.setColour (push::colours::tile);
            g.fillRoundedRectangle (cell, 3.0f);

            juce::Path triangle;
            triangle.addTriangle (cell.getX() + 6.0f, cell.getCentreY() - 6.0f,
                                  cell.getX() + 6.0f, cell.getCentreY() + 6.0f,
                                  cell.getX() + 17.0f, cell.getCentreY());
            g.setColour (push::colours::ledGreen.withAlpha (0.85f));
            g.fillPath (triangle);

            g.setColour (push::colours::text);
            g.setFont (push::scaledFont (11.0f));
            g.drawFittedText (scene.name, cell.toNearestInt().withTrimmedLeft (22).reduced (2, 0),
                              juce::Justification::centredLeft, 1, 1.0f);
        }
    }

    // Stop-Zeile (unten, angepinnt; scrollt horizontal mit)
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (0, getHeight() - stopRowHeight, area.getWidth(), stopRowHeight);

        for (int c = 0; c < (int) columns.size(); ++c)
        {
            const auto x = c * colW - scrollX;

            if (x + colW < 0 || x > area.getWidth())
                continue;

            const auto cell = juce::Rectangle<float> ((float) x + 1.0f,
                                                      (float) (getHeight() - stopRowHeight) + 2.0f,
                                                      (float) colW - 2.0f,
                                                      (float) stopRowHeight - 4.0f);
            g.setColour (push::colours::tile);
            g.fillRoundedRectangle (cell, 3.0f);

            g.setColour (push::colours::textDim);
            g.fillRect (juce::Rectangle<float> (cell.getCentreX() - 4.0f,
                                                cell.getCentreY() - 4.0f, 8.0f, 8.0f));
        }
    }
}

//==============================================================================
void TouchLiveGridView::mouseDown (const juce::MouseEvent&)
{
    panning = false;
    dragStartScroll = { scrollX, scrollY };
}

void TouchLiveGridView::mouseDrag (const juce::MouseEvent& event)
{
    if (! panning && event.getDistanceFromDragStart() < panThreshold)
        return;

    panning = true;
    scrollX = dragStartScroll.x - event.getDistanceFromDragStartX();
    scrollY = dragStartScroll.y - event.getDistanceFromDragStartY();
    clampScroll();
    repaint();
}

void TouchLiveGridView::mouseUp (const juce::MouseEvent& event)
{
    if (panning)
    {
        panning = false;
        return;
    }

    tapAt (event.getPosition());
}

void TouchLiveGridView::tapAt (juce::Point<int> position)
{
    const auto area = gridArea();
    const auto colW = columnWidth();

    if (columns.empty() || colW <= 0)
        return;

    // Scene-Fire-Spalte
    if (position.x >= getWidth() - sceneColumn
        && position.y >= area.getY() && position.y < area.getBottom())
    {
        const auto scene = (position.y - area.getY() + scrollY) / cellHeight;

        if (scene >= 0 && scene < (int) scenes.size())
            fireScene (scene);

        return;
    }

    // Stop-Zeile
    if (position.y >= getHeight() - stopRowHeight && position.x < area.getWidth())
    {
        const auto column = (position.x + scrollX) / colW;

        if (column >= 0 && column < (int) columns.size())
            stopTrack (column);

        return;
    }

    // Clip-Zellen
    if (area.contains (position))
    {
        const auto column = (position.x - area.getX() + scrollX) / colW;
        const auto scene  = (position.y - area.getY() + scrollY) / cellHeight;

        if (column >= 0 && column < (int) columns.size()
            && scene >= 0 && scene < (int) scenes.size())
            fireClip (column, scene);
    }
}

//==============================================================================
void TouchLiveGridView::fireClip (int columnIndex, int sceneIndex)
{
    juce::OSCMessage message { juce::OSCAddressPattern ("/live/clip_slot/fire") };
    message.addString (columns[(size_t) columnIndex].key);   // Stable-ID
    message.addInt32 (sceneIndex);
    client.sendCommand (message);
}

void TouchLiveGridView::fireScene (int sceneIndex)
{
    juce::OSCMessage message { juce::OSCAddressPattern ("/live/scene/fire") };
    message.addInt32 (sceneIndex);
    client.sendCommand (message);
}

void TouchLiveGridView::stopTrack (int columnIndex)
{
    juce::OSCMessage message { juce::OSCAddressPattern ("/live/track/stop_all_clips") };
    message.addString (columns[(size_t) columnIndex].key);
    client.sendCommand (message);
}

} // namespace conduit
