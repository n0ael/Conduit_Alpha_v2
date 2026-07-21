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

    // Fader/XY-Physics (Block J3): laufende Federn einfrieren — die Werte
    // bleiben stehen, wo die Animation gerade war.
    physicsStates.clear();

    ccMode = shouldEdit;
    repaint();
}

void CcControlLayer::setMapMode (bool shouldMap)
{
    if (mapMode == shouldMap)
        return;

    // Wie setCcMode: laufende Spiel-Gesten sauber beenden (gehaltene
    // Push-Controls lösen), Federn einfrieren.
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
    physicsStates.clear();

    mapMode = shouldMap;
    if (! mapMode)
        mapArmedControlId = -1;

    repaint();
}

void CcControlLayer::setMapArmedControl (int controlId)
{
    if (mapArmedControlId == controlId)
        return;

    mapArmedControlId = controlId;
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
    // Frei verschobene Controls (Block F, DIY-Tab) tragen ihren
    // normalisierten Rect selbst; alles andere kommt aus den Zellen.
    if (control.rw > 0.0f)
        return { control.rx * (float) getWidth(), control.ry * (float) getHeight(),
                 control.rw * (float) getWidth(), control.rh * (float) getHeight() };

    return rectForCells (control.c0, control.r0, control.c1, control.r1);
}

int CcControlLayer::controlIdAt (juce::Point<float> position) const noexcept
{
    // Oberstes Control zuerst (zuletzt platziert liegt oben) -- ersetzt den
    // zell-basierten model.controlAt, seit Controls frei liegen koennen.
    const auto& controls = model.controls();

    for (auto it = controls.rbegin(); it != controls.rend(); ++it)
        if (rectFor (*it).contains (position))
            return it->id;

    return -1;
}

juce::Rectangle<float> CcControlLayer::removeZoneFor (juce::Rectangle<float> controlRect) noexcept
{
    return { controlRect.getRight() - kRemoveZoneSide, controlRect.getY(),
             kRemoveZoneSide, kRemoveZoneSide };
}

bool CcControlLayer::hitTest (int x, int y)
{
    if (ccMode || mapMode)
        return true;   // Bearbeiten/Map: ALLE Events über dem Raster abfangen

    // Spielen: nur Control-Flächen sind Ziel — freie Flächen fallen zum
    // Keyboard durch, Pads UNTER Controls bleiben stumm. Rect-basiert,
    // seit Controls frei liegen koennen (Block F).
    return controlIdAt ({ (float) x, (float) y }) >= 0;
}

//==============================================================================
void CcControlLayer::mouseDown (const juce::MouseEvent& event)
{
    // Map-Modus (M5b): Tap auf ein Control meldet den Zuweisungs-Wunsch,
    // alles andere wird geschluckt (kein Spielen, kein Bearbeiten).
    if (mapMode)
    {
        if (const auto id = controlIdAt (event.position); id >= 0 && onMapTapControl != nullptr)
            onMapTapControl (id);
        return;
    }

    if (ccMode)
        handleEditDown (event);
    else
        handlePlayDown (event);
}

void CcControlLayer::mouseDrag (const juce::MouseEvent& event)
{
    if (mapMode)
        return;

    if (ccMode)
        handleEditDrag (event);
    else
        handlePlayDrag (event);
}

void CcControlLayer::mouseUp (const juce::MouseEvent& event)
{
    cursorHider.end();   // Cursor zurück (idempotent, auch ohne aktive Geste)

    if (mapMode)
        return;

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

    const auto id = controlIdAt (event.position);

    if (id >= 0)
    {
        // Bestehendes Control FREI verschieben (Block F): freien Rect aus
        // der aktuellen Geometrie initialisieren, Greif-Offset in Pixeln.
        if (auto* control = model.find (id))
        {
            const auto rect = rectFor (*control);
            const auto width  = juce::jmax (1.0f, (float) getWidth());
            const auto height = juce::jmax (1.0f, (float) getHeight());

            control->rx = rect.getX() / width;
            control->ry = rect.getY() / height;
            control->rw = rect.getWidth() / width;
            control->rh = rect.getHeight() / height;

            movingId     = id;
            moveGrabPx   = event.position - rect.getPosition();
            editFinger   = finger;
        }
        return;
    }

    if (activeTool != grid::CcTool::none)
    {
        const auto cell = cellAt (event.position);
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

    if (placing)
    {
        const auto cell = cellAt (event.position);   // beide Ecken geklemmt ins Raster
        if (cell.c != placeCurrent.c || cell.r != placeCurrent.r)
        {
            placeCurrent = cell;
            repaint();
        }
        return;
    }

    if (movingId < 0)
        return;

    auto* control = model.find (movingId);
    if (control == nullptr)
        return;

    const auto width  = juce::jmax (1.0f, (float) getWidth());
    const auto height = juce::jmax (1.0f, (float) getHeight());

    // Rohe Zielposition (normalisiert) aus dem Zeiger + Greif-Offset --
    // Figma-Snapping rechnet IMMER von der rohen Position aus (kleines
    // Ausweichen ueber die Schwelle loest die Flucht wieder).
    const auto rawOrigin = (event.position - moveGrabPx);
    const juce::Rectangle<float> moving { rawOrigin.x / width, rawOrigin.y / height,
                                          control->rw, control->rh };

    std::vector<juce::Rectangle<float>> others;
    others.reserve (model.controls().size());
    for (const auto& other : model.controls())
        if (other.id != movingId)
            others.push_back ({ rectFor (other).getX() / width, rectFor (other).getY() / height,
                                rectFor (other).getWidth() / width, rectFor (other).getHeight() / height });

    const auto snapped = grid::FigmaSnap::snap (moving, others,
                                                kSnapThresholdPx / width,
                                                kSnapThresholdPx / height);

    control->rx = juce::jlimit (0.0f, juce::jmax (0.0f, 1.0f - control->rw), snapped.x);
    control->ry = juce::jlimit (0.0f, juce::jmax (0.0f, 1.0f - control->rh), snapped.y);

    activeSnap = snapped;
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
    activeSnap = {};
    repaint();
}

//==============================================================================
void CcControlLayer::handlePlayDown (const juce::MouseEvent& event)
{
    const auto id = controlIdAt (event.position);
    if (id < 0)
        return;   // hitTest lässt freie Flächen gar nicht erst hierher

    auto* control = model.find (id);
    if (control == nullptr)
        return;

    grabbedControls[event.source.getIndex()] = id;

    // Long-Press (Block E): Halten ohne nennenswerte Bewegung oeffnet die
    // Macro-Ansicht (Muster AxisColourRow). Nur EIN Kandidat zur Zeit --
    // ein zweiter Finger bricht den laufenden Kandidaten nicht ab, startet
    // aber keinen neuen.
    if (longPressFinger < 0)
    {
        longPressFinger = event.source.getIndex();
        longPressControlId = id;
        longPressStart = event.position;
        startTimer (kLongPressMs);
    }

    applyPlayGesture (*control, event.position, true);
    repaint();
}

void CcControlLayer::handlePlayDrag (const juce::MouseEvent& event)
{
    const auto it = grabbedControls.find (event.source.getIndex());
    if (it == grabbedControls.end())
        return;

    // Bewegung ueber der Toleranz bricht den Long-Press ab (ein Fader-
    // Sweep/XY-Drag darf die Macro-Ansicht nicht oeffnen).
    if (event.source.getIndex() == longPressFinger
        && event.position.getDistanceFrom (longPressStart) > kLongPressMoveTolerancePx)
        cancelLongPress();

    if (auto* control = model.find (it->second))
    {
        // Nur die Zielflächen (Fader/XY, Wert springt zum Finger) blenden den
        // Cursor aus — Push/Toggle sind diskrete Taps und behalten ihn.
        if (control->type == grid::CcTool::fader || control->type == grid::CcTool::xy)
            cursorHider.begin (*this, event, ui::DragCursorHider::Mode::absolute);

        applyPlayGesture (*control, event.position, false);
        repaint();
    }
}

void CcControlLayer::handlePlayUp (const juce::MouseEvent& event)
{
    const auto it = grabbedControls.find (event.source.getIndex());
    if (it == grabbedControls.end())
        return;

    if (event.source.getIndex() == longPressFinger)
        cancelLongPress();

    if (auto* control = model.find (it->second);
        control != nullptr && control->type == grid::CcTool::push && control->on)
    {
        control->on = false;   // Push: an nur solange gehalten
        notifyValueChanged (*control);
    }

    // Fader/XY-Physics (Block J3): Loslassen gibt die Feder frei — mit
    // Snap-to-Default federt der Wert auf den Default zurück, sonst
    // schwingt er am letzten Ziel aus.
    if (auto* control = model.find (it->second);
        control != nullptr && physicsEnabled()
        && (control->type == grid::CcTool::fader || control->type == grid::CcTool::xy))
    {
        const auto physicsIt = physicsStates.find (control->id);
        if (physicsIt != physicsStates.end())
        {
            physicsIt->second.grabbed = false;

            if (panelSettings->isControlSnapToDefault())
            {
                const auto defaults = physicsDefaultsFor (control->type);
                physicsIt->second.targetValue = defaults.value;
                physicsIt->second.targetX = defaults.x;
                physicsIt->second.targetY = defaults.y;
            }
        }
    }

    grabbedControls.erase (it);
    repaint();
}

void CcControlLayer::cancelLongPress()
{
    stopTimer();
    longPressFinger = -1;
    longPressControlId = -1;
}

void CcControlLayer::timerCallback()
{
    const auto id = longPressControlId;
    cancelLongPress();

    if (id >= 0 && onLongPressControl != nullptr)
        onLongPressControl (id);
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
                const auto raw = juce::jlimit (0.0f, 1.0f,
                                               (rect.getBottom() - position.y) / rect.getHeight());

                // Fader/XY-Physics (Block J3): der Finger setzt nur das
                // ZIEL — der gesendete Wert folgt über die Feder
                // (physicsTick schreibt control.value).
                if (physicsEnabled())
                {
                    auto& physics = physicsFor (control);
                    physics.grabbed = true;
                    physics.targetValue = raw;
                    break;
                }

                control.value = raw;
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
                const auto rawX = juce::jlimit (0.0f, 1.0f, (position.x - inner.getX()) / inner.getWidth());
                const auto rawY = juce::jlimit (0.0f, 1.0f, (position.y - inner.getY()) / inner.getHeight());

                if (physicsEnabled())   // Block J3, wie beim Fader
                {
                    auto& physics = physicsFor (control);
                    physics.grabbed = true;
                    physics.targetX = rawX;
                    physics.targetY = rawY;
                    break;
                }

                control.x = rawX;
                control.y = rawY;
                notifyValueChanged (control);
            }
            break;
        }

        case grid::CcTool::none:
            break;
    }
}

//==============================================================================
// Fader/XY-Physics (Block J3)

grid::CcControl CcControlLayer::physicsDefaultsFor (grid::CcTool type) noexcept
{
    // Snap-Ziel = die Default-Feldwerte eines frisch platzierten Controls
    // (CcControl-Initialisierer: Fader 0.75, XY 0.5/0.5).
    grid::CcControl defaults;
    defaults.type = type;
    return defaults;
}

CcControlLayer::PhysicsState& CcControlLayer::physicsFor (const grid::CcControl& control)
{
    const auto it = physicsStates.find (control.id);
    if (it != physicsStates.end())
        return it->second;

    // Feder am Ist-Wert starten (kein Sprung beim Greifen).
    PhysicsState state;
    state.value.pos   = control.value;
    state.targetValue = control.value;
    state.x.pos = control.x;
    state.y.pos = control.y;
    state.targetX = control.x;
    state.targetY = control.y;

    return physicsStates.emplace (control.id, state).first->second;
}

void CcControlLayer::physicsTick()
{
    if (! physicsEnabled())
    {
        if (! physicsStates.empty())
            physicsStates.clear();

        lastPhysicsTickMs = 0.0;
        return;
    }

    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    const auto dtSeconds = lastPhysicsTickMs > 0.0
                               ? juce::jmin (0.1f, (float) ((nowMs - lastPhysicsTickMs) / 1000.0))
                               : 0.0f;
    lastPhysicsTickMs = nowMs;

    if (physicsStates.empty() || dtSeconds <= 0.0f)
        return;

    grid::SpringParams params;
    params.force     = (float) panelSettings->getPhysicsForce();
    params.mass      = (float) panelSettings->getPhysicsMass();
    params.inertia01 = (float) panelSettings->getPhysicsInertia() / 100.0f;

    constexpr float kSettlePos = 1.0e-3f;
    constexpr float kSettleVel = 1.0e-2f;
    constexpr float kNotifyEps = 1.0e-4f;

    auto anyStepped = false;

    for (auto it = physicsStates.begin(); it != physicsStates.end();)
    {
        auto* control = model.find (it->first);
        if (control == nullptr)
        {
            it = physicsStates.erase (it);   // Control wurde entfernt
            continue;
        }

        auto& state = it->second;

        grid::stepSpring (state.value, state.targetValue, params, dtSeconds);
        grid::stepSpring (state.x, state.targetX, params, dtSeconds);
        grid::stepSpring (state.y, state.targetY, params, dtSeconds);

        const auto newValue = juce::jlimit (0.0f, 1.0f, state.value.pos);
        const auto newX     = juce::jlimit (0.0f, 1.0f, state.x.pos);
        const auto newY     = juce::jlimit (0.0f, 1.0f, state.y.pos);

        if (std::abs (newValue - control->value) > kNotifyEps
            || std::abs (newX - control->x) > kNotifyEps
            || std::abs (newY - control->y) > kNotifyEps)
        {
            control->value = newValue;
            control->x = newX;
            control->y = newY;
            notifyValueChanged (*control);
            anyStepped = true;
        }

        const auto settled = ! state.grabbed
                             && std::abs (state.value.pos - state.targetValue) < kSettlePos
                             && std::abs (state.value.vel) < kSettleVel
                             && std::abs (state.x.pos - state.targetX) < kSettlePos
                             && std::abs (state.x.vel) < kSettleVel
                             && std::abs (state.y.pos - state.targetY) < kSettlePos
                             && std::abs (state.y.vel) < kSettleVel;

        if (settled)
        {
            // Exakt aufs Ziel schreiben und den Zustand freigeben.
            control->value = juce::jlimit (0.0f, 1.0f, state.targetValue);
            control->x = juce::jlimit (0.0f, 1.0f, state.targetX);
            control->y = juce::jlimit (0.0f, 1.0f, state.targetY);
            notifyValueChanged (*control);
            it = physicsStates.erase (it);
            anyStepped = true;
            continue;
        }

        ++it;
    }

    if (anyStepped)
        repaint();
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

    // Macro-Modulation (M5c): cyaner Zweit-Marker am Effektivwert — nur
    // fuer Controls mit aktiver Modulation (Provider liefert sonst nullopt).
    if (modulationValueFor != nullptr)
    {
        for (const auto& control : model.controls())
        {
            const auto rect = rectFor (control);

            if (control.type == grid::CcTool::fader)
            {
                if (const auto value = modulationValueFor (control.id, 0))
                {
                    const auto y = rect.getBottom() - *value * rect.getHeight();
                    g.setColour (push::colours::ledCyan);
                    g.drawLine (rect.getX() + 2.0f, y, rect.getRight() - 2.0f, y, 2.0f);
                }
            }
            else if (control.type == grid::CcTool::xy)
            {
                const auto valueX = modulationValueFor (control.id, 0);
                const auto valueY = modulationValueFor (control.id, 1);

                if (valueX.has_value() || valueY.has_value())
                {
                    // Fehlende Achse faellt auf den Ist-Wert zurueck
                    // (axis 1 ist in der Macro-Semantik invertiert).
                    const auto px = rect.getX()
                                    + (valueX.has_value() ? *valueX : control.x) * rect.getWidth();
                    const auto py = rect.getY()
                                    + (valueY.has_value() ? 1.0f - *valueY : control.y) * rect.getHeight();
                    g.setColour (push::colours::ledCyan);
                    g.drawEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ px, py }), 2.0f);
                }
            }
        }
    }

    // Map-Modus (M5b): alle Controls hervorheben — cyaner Rahmen (Learn-
    // scharfes Control orange), Badge mit der gebundenen Adresse unten im
    // Control (leer = ungebunden, nur Rahmen).
    if (mapMode)
    {
        for (const auto& control : model.controls())
        {
            const auto rect  = rectFor (control);
            const auto armed = control.id == mapArmedControlId;

            g.setColour ((armed ? push::colours::ledOrange : push::colours::ledCyan)
                             .withAlpha (armed ? 1.0f : 0.75f));
            g.drawRoundedRectangle (rect, kCornerRadius, armed ? 2.5f : 1.5f);

            const auto badge = mapBadgeTextFor != nullptr ? mapBadgeTextFor (control.id)
                                                          : juce::String();
            if (badge.isNotEmpty())
            {
                auto badgeArea = rect.reduced (4.0f).removeFromBottom (14.0f);
                g.setColour (push::colours::lcdScreen.withAlpha (0.8f));
                g.fillRoundedRectangle (badgeArea, 3.0f);
                g.setColour (armed ? push::colours::ledOrange : push::colours::text);
                g.setFont (push::scaledFont (11.0f));
                // Nie stauchen (User-Regel 07/2026): drawText clippt statt
                // zu quetschen.
                g.drawText (badge, badgeArea.reduced (3.0f, 0.0f),
                            juce::Justification::centredLeft, false);
            }
        }
    }

    // Snap-Guides (Block F, Figma-Stil): waehrend eines freien Verschiebens
    // zeigt eine duenne Linie die eingerastete Mittel-Achse.
    if (ccMode && movingId >= 0)
    {
        g.setColour (push::colours::ledCyan.withAlpha (0.8f));

        if (activeSnap.snappedX)
        {
            const auto x = activeSnap.guideX * (float) getWidth();
            g.drawLine (x, 0.0f, x, (float) getHeight(), 1.0f);
        }
        if (activeSnap.snappedY)
        {
            const auto y = activeSnap.guideY * (float) getHeight();
            g.drawLine (0.0f, y, (float) getWidth(), y, 1.0f);
        }
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

            // Zweifarbige Anzeige (Block J3): das Feder-ZIEL als cyane
            // Linie, der weiße Balken ist der gesendete Ist-Wert.
            if (const auto physicsIt = physicsStates.find (control.id);
                physicsIt != physicsStates.end()
                && std::abs (physicsIt->second.targetValue - control.value) > 0.002f)
            {
                const auto targetTop = rect.getBottom()
                                       - rect.getHeight() * physicsIt->second.targetValue;
                g.setColour (push::colours::ledCyan.withAlpha (0.9f));
                g.fillRect (juce::Rectangle<float> (rect.getX(), targetTop - 1.0f,
                                                    rect.getWidth(), 2.0f));
            }
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

            // Zweifarbige Anzeige (Block J3): Geister-Ring am Feder-ZIEL in
            // cyan — der weiße Handle ist der gesendete Ist-Wert.
            if (const auto physicsIt = physicsStates.find (control.id);
                physicsIt != physicsStates.end()
                && (std::abs (physicsIt->second.targetX - control.x) > 0.002f
                    || std::abs (physicsIt->second.targetY - control.y) > 0.002f))
            {
                const auto ghost = juce::Rectangle<float> (kXyHandleSize, kXyHandleSize)
                                       .withCentre ({ inner.getX() + inner.getWidth() * physicsIt->second.targetX,
                                                      inner.getY() + inner.getHeight() * physicsIt->second.targetY });
                g.setColour (push::colours::ledCyan.withAlpha (0.7f));
                g.drawEllipse (ghost, 1.5f);
            }

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
