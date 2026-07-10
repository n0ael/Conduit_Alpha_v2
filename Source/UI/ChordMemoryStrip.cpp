#include "ChordMemoryStrip.h"

#include <cmath>

#include "PushLookAndFeel.h"

namespace conduit
{

ChordMemoryStrip::ChordMemoryStrip (grid::ChordMemory& memoryToUse)
    : memory (memoryToUse)
{
    setWantsKeyboardFocus (false);
}

void ChordMemoryStrip::setSurfaceAspect (float widthOverHeight) noexcept
{
    if (widthOverHeight > 0.0f)
    {
        surfaceAspect = widthOverHeight;
        repaint();
    }
}

juce::Rectangle<float> ChordMemoryStrip::slotBounds (int slot) const noexcept
{
    const auto area = getLocalBounds().toFloat().reduced ((float) kPaddingPx);
    const auto totalGap = (float) kGapPx * (float) (grid::ChordMemory::kNumSlots - 1);
    const auto slotHeight = (area.getHeight() - totalGap) / (float) grid::ChordMemory::kNumSlots;

    if (slotHeight <= 0.0f || area.getWidth() <= 0.0f)
        return {};

    return { area.getX(), area.getY() + (float) slot * (slotHeight + (float) kGapPx),
             area.getWidth(), slotHeight };
}

int ChordMemoryStrip::slotAt (juce::Point<float> position) const noexcept
{
    for (int slot = 0; slot < grid::ChordMemory::kNumSlots; ++slot)
        if (slotBounds (slot).contains (position))
            return slot;

    return -1;
}

//==============================================================================
void ChordMemoryStrip::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::background);

    for (int slot = 0; slot < grid::ChordMemory::kNumSlots; ++slot)
    {
        const auto bounds = slotBounds (slot);
        if (bounds.isEmpty())
            continue;

        // "LCD-Screen": rein schwarze Fläche (KEIN Verlauf), 1-px-Kontur.
        g.setColour (push::colours::lcdScreen);
        g.fillRoundedRectangle (bounds, 4.0f);

        // Slot-Nummer klein unten rechts.
        g.setColour (push::colours::controlOutline);
        g.setFont (push::scaledFont (9.0f));
        g.drawText (juce::String (slot + 1), bounds.reduced (4.0f, 2.0f),
                    juce::Justification::bottomRight, false);

        if (memory.isOccupied (slot))
        {
            // Mini-Ansicht direkt aus dem Slot (const ref, keine Kopien);
            // Punkte dürfen über den Slot-Rand — Clipping auf die Fläche.
            juce::Graphics::ScopedSaveState clipScope (g);
            g.reduceClipRegion (bounds.getSmallestIntegerContainer());

            const auto slotW = bounds.getWidth();
            const auto slotH = bounds.getHeight();

            for (const auto& sun : memory.slot (slot))
            {
                const juce::Point<float> sunPos (bounds.getX() + sun.x * slotW,
                                                 bounds.getY() + sun.y * slotH);

                if (sun.hasOrbit)
                {
                    // Orbit als Ellipse (Mock renderVals): ox/oy sind über
                    // die Flächen-BREITE normalisiert, die y-Achse wird über
                    // den Spielflächen-Aspekt gestaucht.
                    const auto r  = std::hypot (sun.ox, sun.oy);
                    const auto rx = r * slotW;
                    const auto ry = r * slotW * surfaceAspect;

                    g.setColour (push::colours::ledWhite.withAlpha (0.55f));
                    g.drawEllipse (juce::Rectangle<float> (rx * 2.0f, ry * 2.0f).withCentre (sunPos), 1.0f);

                    const juce::Point<float> moonPos (
                        bounds.getX() + (sun.x + sun.ox) * slotW,
                        bounds.getY() + (sun.y + sun.oy * surfaceAspect) * slotH);

                    g.setColour (push::colours::ledWhite);
                    g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (moonPos));
                }

                g.setColour (push::colours::ledWhite);
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (sunPos));
            }
        }

        g.setColour (push::colours::outline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
    }
}

//==============================================================================
void ChordMemoryStrip::mouseDown (const juce::MouseEvent& event)
{
    const auto slot = slotAt (event.position);
    if (slot < 0)
        return;

    const auto occupied = memory.isOccupied (slot);

    if (isCcMode && isCcMode() && occupied)
    {
        // CC-Modus = Bearbeiten: Tap löscht den Slot (einzige Art, einen
        // belegten Slot wieder freizugeben — store überschreibt nie).
        memory.clear (slot);
        repaint();
        return;
    }

    if (! occupied)
    {
        if (getConstellation)
        {
            auto suns = getConstellation();

            if (! suns.empty() && memory.store (slot, std::move (suns)))
                repaint();
        }

        return;
    }

    // Belegter Slot (MPE-Modus): Konstellation abrufen und Drag-Session
    // starten — Ziehen verschiebt den Akkord starr.
    if (onRecall)
        onRecall (slot);

    if (dragSourceIndex < 0)
    {
        dragSourceIndex  = event.source.getIndex();
        lastDragPosition = event.position;
    }
}

void ChordMemoryStrip::mouseDrag (const juce::MouseEvent& event)
{
    if (dragSourceIndex != event.source.getIndex())
        return;

    const auto delta = event.position - lastDragPosition;
    lastDragPosition = event.position;

    // Pixel-Deltas — Strip und Keyboard haben denselben Screen-Maßstab.
    if (onMoveBy)
        onMoveBy (delta.x, delta.y);
}

void ChordMemoryStrip::mouseUp (const juce::MouseEvent& event)
{
    // Loslassen stoppt nur das Verschieben — die Konstellation bleibt
    // latched auf dem Grid liegen.
    if (dragSourceIndex == event.source.getIndex())
        dragSourceIndex = -1;
}

} // namespace conduit
