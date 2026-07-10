#include "CcControlLayer.h"

#include <cmath>

#include "PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr float kCornerRadius   = 4.0f;
    constexpr float kOutlineWidth   = 1.5f;
    constexpr float kRemoveZoneSide = 18.0f;
    constexpr float kXyHandleSize   = 14.0f;
    constexpr float kXyInset        = 8.0f;   // Bewegungsfläche des XY-Handles

    [[nodiscard]] const char* labelFor (conduit::grid::CcTool tool) noexcept
    {
        switch (tool)
        {
            case conduit::grid::CcTool::fader:  return "Fader";
            case conduit::grid::CcTool::push:   return "Push";
            case conduit::grid::CcTool::toggle: return "Toggle";
            case conduit::grid::CcTool::xy:     return "XY";
            case conduit::grid::CcTool::none:   break;
        }
        return "";
    }
}

CcControlLayer::CcControlLayer (grid::CcControlModel& modelToUse, int colsToUse, int rowsToUse)
    : model (modelToUse), cols (juce::jmax (1, colsToUse)), rows (juce::jmax (1, rowsToUse))
{
    setWantsKeyboardFocus (false);
    setInterceptsMouseClicks (true, false);   // keine Kinder — alles im Layer
}

void CcControlLayer::setCcMode (bool shouldEdit)
{
    if (ccMode == shouldEdit)
        return;

    // Laufende Spiel-Gesten sauber beenden: gehaltene Push-Controls lösen.
    for (const auto& [finger, id] : grabbedControls)
    {
        juce::ignoreUnused (finger);
        if (auto* control = model.find (id);
            control != nullptr && control->type == grid::CcTool::push && control->on)
        {
            control->on = false;
            notifyValueChanged (*control);
        }
    }

    grabbedControls.clear();
    placing    = false;
    movingId   = -1;
    editFinger = -1;

    ccMode = shouldEdit;
    repaint();
}

void CcControlLayer::setActiveTool (grid::CcTool tool)
{
    if (activeTool == tool)
        return;

    activeTool = tool;
    repaint();
}

//==============================================================================
CcControlLayer::Cell CcControlLayer::cellAt (juce::Point<float> position) const noexcept
{
    const auto width  = (float) getWidth();
    const auto height = (float) getHeight();
    if (width <= 0.0f || height <= 0.0f)
        return {};

    return { juce::jlimit (0, cols - 1, (int) std::floor (position.x / (width  / (float) cols))),
             juce::jlimit (0, rows - 1, (int) std::floor (position.y / (height / (float) rows))) };
}

juce::Rectangle<float> CcControlLayer::rectForCells (int c0, int r0, int c1, int r1) const noexcept
{
    const auto cellWidth  = (float) getWidth()  / (float) cols;
    const auto cellHeight = (float) getHeight() / (float) rows;

    return juce::Rectangle<float> ((float) c0 * cellWidth, (float) r0 * cellHeight,
                                   (float) (c1 - c0 + 1) * cellWidth,
                                   (float) (r1 - r0 + 1) * cellHeight)
        .reduced (1.0f);   // Mock: 1 px Padding um die Control-Fläche
}

juce::Rectangle<float> CcControlLayer::rectFor (const grid::CcControl& control) const noexcept
{
    return rectForCells (control.c0, control.r0, control.c1, control.r1);
}

juce::Rectangle<float> CcControlLayer::removeZoneFor (juce::Rectangle<float> controlRect) noexcept
{
    return { controlRect.getRight() - kRemoveZoneSide, controlRect.getY(),
             kRemoveZoneSide, kRemoveZoneSide };
}

bool CcControlLayer::hitTest (int x, int y)
{
    if (ccMode)
        return true;   // Bearbeiten: ALLE Events über dem Raster abfangen

    // Spielen: nur Control-Flächen sind Ziel — freie Flächen fallen zum
    // Keyboard durch, Pads UNTER Controls bleiben stumm.
    const auto cell = cellAt ({ (float) x, (float) y });
    return model.controlAt (cell.c, cell.r) >= 0;
}

//==============================================================================
void CcControlLayer::mouseDown (const juce::MouseEvent& event)
{
    if (ccMode)
        handleEditDown (event);
    else
        handlePlayDown (event);
}

void CcControlLayer::mouseDrag (const juce::MouseEvent& event)
{
    if (ccMode)
        handleEditDrag (event);
    else
        handlePlayDrag (event);
}

void CcControlLayer::mouseUp (const juce::MouseEvent& event)
{
    if (ccMode)
        handleEditUp (event);
    else
        handlePlayUp (event);
}

//==============================================================================
void CcControlLayer::handleEditDown (const juce::MouseEvent& event)
{
    const auto finger = event.source.getIndex();
    if (editFinger >= 0 && finger != editFinger)
        return;   // ein Edit-Vorgang zur Zeit

    // ×-Zone zuerst: Tap aufs oberste Control unter dem Punkt entfernt es.
    // Rückwärts = oberstes zuerst; liegt der Punkt auf einer Control-Fläche
    // (aber nicht deren ×), verdeckt sie die ×-Zonen darunter.
    const auto& controls = model.controls();
    for (auto it = controls.rbegin(); it != controls.rend(); ++it)
    {
        const auto rect = rectFor (*it);

        if (removeZoneFor (rect).contains (event.position))
        {
            model.remove (it->id);
            repaint();
            return;
        }

        if (rect.contains (event.position))
            break;
    }

    const auto cell = cellAt (event.position);
    const auto id = model.controlAt (cell.c, cell.r);

    if (id >= 0)
    {
        // Bestehendes Control verschieben: Offset Greifzelle → Ursprung merken.
        if (const auto* control = model.find (id))
        {
            movingId       = id;
            moveGrabOffset = { cell.c - control->c0, cell.r - control->r0 };
            editFinger     = finger;
        }
        return;
    }

    if (activeTool != grid::CcTool::none)
    {
        placing      = true;
        placeStart   = cell;
        placeCurrent = cell;
        editFinger   = finger;
        repaint();
    }

    // Ohne Werkzeug + freie Fläche: Event schlucken (keine Noten im CC-Modus).
}

void CcControlLayer::handleEditDrag (const juce::MouseEvent& event)
{
    if (event.source.getIndex() != editFinger)
        return;

    const auto cell = cellAt (event.position);   // beide Ecken geklemmt ins Raster

    if (placing)
    {
        if (cell.c != placeCurrent.c || cell.r != placeCurrent.r)
        {
            placeCurrent = cell;
            repaint();
        }
        return;
    }

    if (movingId >= 0
        && model.moveTo (movingId, cell.c - moveGrabOffset.c, cell.r - moveGrabOffset.r,
                         cols, rows))
        repaint();
}

void CcControlLayer::handleEditUp (const juce::MouseEvent& event)
{
    if (event.source.getIndex() != editFinger)
        return;

    if (placing)
    {
        if (activeTool != grid::CcTool::none)
            model.addControl (activeTool, placeStart.c, placeStart.r,
                              placeCurrent.c, placeCurrent.r);
        placing = false;
        repaint();
    }

    movingId   = -1;
    editFinger = -1;
}

//==============================================================================
void CcControlLayer::handlePlayDown (const juce::MouseEvent& event)
{
    const auto cell = cellAt (event.position);
    const auto id = model.controlAt (cell.c, cell.r);
    if (id < 0)
        return;   // hitTest lässt freie Flächen gar nicht erst hierher

    auto* control = model.find (id);
    if (control == nullptr)
        return;

    grabbedControls[event.source.getIndex()] = id;
    applyPlayGesture (*control, event.position, true);
    repaint();
}

void CcControlLayer::handlePlayDrag (const juce::MouseEvent& event)
{
    const auto it = grabbedControls.find (event.source.getIndex());
    if (it == grabbedControls.end())
        return;

    if (auto* control = model.find (it->second))
    {
        applyPlayGesture (*control, event.position, false);
        repaint();
    }
}

void CcControlLayer::handlePlayUp (const juce::MouseEvent& event)
{
    const auto it = grabbedControls.find (event.source.getIndex());
    if (it == grabbedControls.end())
        return;

    if (auto* control = model.find (it->second);
        control != nullptr && control->type == grid::CcTool::push && control->on)
    {
        control->on = false;   // Push: an nur solange gehalten
        notifyValueChanged (*control);
    }

    grabbedControls.erase (it);
    repaint();
}

void CcControlLayer::applyPlayGesture (grid::CcControl& control,
                                       juce::Point<float> position, bool isDown)
{
    const auto rect = rectFor (control);

    switch (control.type)
    {
        case grid::CcTool::fader:
            if (rect.getHeight() > 0.0f)
            {
                control.value = juce::jlimit (0.0f, 1.0f,
                                              (rect.getBottom() - position.y) / rect.getHeight());
                notifyValueChanged (control);
            }
            break;

        case grid::CcTool::push:
            if (isDown && ! control.on)
            {
                control.on = true;
                notifyValueChanged (control);
            }
            break;

        case grid::CcTool::toggle:
            if (isDown)
            {
                control.on = ! control.on;
                notifyValueChanged (control);
            }
            break;

        case grid::CcTool::xy:
        {
            // Handle-Bewegungsfläche = Control-Fläche minus Inset, damit der
            // 14-px-Kreis auch an den Extremen komplett sichtbar bleibt.
            const auto inner = rect.reduced (kXyInset);
            if (inner.getWidth() > 0.0f && inner.getHeight() > 0.0f)
            {
                control.x = juce::jlimit (0.0f, 1.0f, (position.x - inner.getX()) / inner.getWidth());
                control.y = juce::jlimit (0.0f, 1.0f, (position.y - inner.getY()) / inner.getHeight());
                notifyValueChanged (control);
            }
            break;
        }

        case grid::CcTool::none:
            break;
    }
}

void CcControlLayer::notifyValueChanged (const grid::CcControl& control)
{
    // TODO(design): MIDI-CC-Versand dockt hier an (CC-Nummern-Zuweisung pro
    // Control folgt) — vorerst nur UI-State im Modell + Callback-Stub.
    if (onControlValueChanged != nullptr)
        onControlValueChanged (control);
}

//==============================================================================
void CcControlLayer::paint (juce::Graphics& g)
{
    for (const auto& control : model.controls())
        drawControl (g, control);

    // Platzierungs-Vorschau (CC-Modus): gestrichelte ledWhite-Kontur + zarte
    // Füllung über der Zell-Union von Start- bis aktueller Zelle.
    if (ccMode && placing)
    {
        const auto rect = rectForCells (juce::jmin (placeStart.c, placeCurrent.c),
                                        juce::jmin (placeStart.r, placeCurrent.r),
                                        juce::jmax (placeStart.c, placeCurrent.c),
                                        juce::jmax (placeStart.r, placeCurrent.r));

        g.setColour (push::colours::ledWhite.withAlpha (0.08f));
        g.fillRoundedRectangle (rect, kCornerRadius);

        juce::Path outline;
        outline.addRoundedRectangle (rect, kCornerRadius);

        juce::Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (kOutlineWidth).createDashedStroke (dashed, outline,
                                                                 dashes, 2);
        g.setColour (push::colours::ledWhite);
        g.fillPath (dashed);
    }
}

void CcControlLayer::drawControl (juce::Graphics& g, const grid::CcControl& control) const
{
    const auto rect = rectFor (control);

    g.setColour (push::colours::controlSurface);
    g.fillRoundedRectangle (rect, kCornerRadius);

    switch (control.type)
    {
        case grid::CcTool::fader:
        {
            // Füllbalken von unten bis zum Wert, an die runde Fläche geclippt.
            const auto fillTop = rect.getBottom() - rect.getHeight() * control.value;

            g.saveState();
            juce::Path clip;
            clip.addRoundedRectangle (rect, kCornerRadius);
            g.reduceClipRegion (clip);

            g.setColour (push::colours::ledWhite.withAlpha (0.25f));
            g.fillRect (rect.withTop (fillTop));
            g.setColour (push::colours::ledWhite);
            g.fillRect (juce::Rectangle<float> (rect.getX(), fillTop - 1.0f,
                                                rect.getWidth(), 2.0f));
            g.restoreState();
            break;
        }

        case grid::CcTool::push:
            if (control.on)
            {
                g.setColour (push::colours::ledWhite.withAlpha (0.85f));
                g.fillRoundedRectangle (rect, kCornerRadius);
            }
            break;

        case grid::CcTool::toggle:
        {
            // Pill-Indikator mittig: Outline + gefüllter Kreis (rechts = an).
            const auto pillWidth  = juce::jmin (rect.getWidth() * 0.5f, 44.0f);
            const auto pillHeight = juce::jmin (rect.getHeight() * 0.35f, 22.0f);
            const auto pill = juce::Rectangle<float> (pillWidth, pillHeight)
                                  .withCentre (rect.getCentre());

            g.setColour (control.on ? push::colours::ledWhite : push::colours::controlOutline);
            g.drawRoundedRectangle (pill, pillHeight * 0.5f, kOutlineWidth);

            const auto knobDiameter = pillHeight * 0.6f;
            const auto knobX = control.on ? pill.getRight() - pillHeight * 0.5f
                                          : pill.getX() + pillHeight * 0.5f;
            g.setColour (control.on ? push::colours::ledWhite : push::colours::textDim);
            g.fillEllipse (juce::Rectangle<float> (knobDiameter, knobDiameter)
                               .withCentre ({ knobX, pill.getCentreY() }));
            break;
        }

        case grid::CcTool::xy:
        {
            const auto inner = rect.reduced (kXyInset);
            const auto handle = juce::Rectangle<float> (kXyHandleSize, kXyHandleSize)
                                    .withCentre ({ inner.getX() + inner.getWidth() * control.x,
                                                   inner.getY() + inner.getHeight() * control.y });

            // Handle: Füllung = Control-Fläche, 2-px-ledWhite-Kontur (Mock).
            g.setColour (push::colours::controlSurface);
            g.fillEllipse (handle);
            g.setColour (push::colours::ledWhite);
            g.drawEllipse (handle, 2.0f);
            break;
        }

        case grid::CcTool::none:
            break;
    }

    g.setColour (push::colours::controlOutline);
    g.drawRoundedRectangle (rect.reduced (kOutlineWidth * 0.5f), kCornerRadius, kOutlineWidth);

    // Label unten links (Inset 5,3 — Mock).
    g.setColour (push::colours::textDim);
    g.setFont (push::scaledFont (10.0f));
    g.drawText (labelFor (control.type),
                juce::Rectangle<float> (rect.getX() + 5.0f, rect.getBottom() - 3.0f - 12.0f,
                                        juce::jmax (0.0f, rect.getWidth() - 10.0f), 12.0f),
                juce::Justification::centredLeft, false);

    // ×-Zone (nur im CC-Modus): 18×18 oben rechts — ×(U+00D7) statt ✕, sicher
    // im Jost-Font (Anti-Tofu); Nicht-ASCII via fromUTF8 (Rule ui-design).
    if (ccMode)
    {
        g.setColour (push::colours::textDim);
        g.setFont (push::scaledFont (11.0f));
        g.drawText (juce::String::fromUTF8 ("\xc3\x97"), removeZoneFor (rect),
                    juce::Justification::centred, false);
    }
}

} // namespace conduit
