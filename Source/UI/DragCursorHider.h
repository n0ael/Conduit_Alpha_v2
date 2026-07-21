#pragma once

#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace conduit::ui
{

//==============================================================================
/**
    Blendet den Maus-Cursor WÄHREND einer Ziehgeste aus, damit der Pfeil den
    Punkt/Wert unter dem Finger nicht verdeckt (User-Wunsch 21.07.2026 —
    Pilot am Looper-Distanz-Pad, danach app-weites Ausrollen).

    Zwei Sorten Zielbewegung brauchen ein leicht unterschiedliches Ausblenden:

    - `relative`  — der Wert folgt dem WISCHWEG, nicht der absoluten Position
      (Pads, Drehregler, Wisch-Fader). Nutzt JUCEs „unbounded mouse
      movement": der Cursor verschwindet, die Maus wandert unbegrenzt weiter
      (kein Anstoßen am Bildschirmrand), und beim Ende springt der Cursor an
      die ZUPACK-Stelle zurück. `event.position` / `getOffsetFromDragStart()`
      bleiben dabei stetig — die relative Zieh-Mathematik ist unberührt.

    - `absolute`  — der gezeichnete Punkt folgt dem Cursor DIREKT
      (On-Screen-XY, Kurven-Editoren, Grid-Sonne). Hier IST der Punkt schon
      der Cursor: wir verstecken nur den Pfeil (`MouseCursor::NoCursor`) und
      stellen den vorherigen Cursor am Ende wieder her — die echte
      Cursorposition bleibt erhalten.

    - `crosshair` — wie `absolute`, aber statt den Cursor zu verstecken wird
      er zum Fadenkreuz „+" (`MouseCursor::CrosshairCursor`). Fürs
      Node-Kabel-Ziehen: das Kabelende zielt präzise auf einen Port, ein
      sichtbares Kreuz hilft beim Treffen (User-Wunsch 22.07.2026).

    Greift NUR bei echtem Zeiger-Input (Maus/Trackpad) und nur, wenn das
    Component tatsächlich auf einem Fenster liegt (`getPeer()`): Touch hat nie
    einen Cursor, und headless Tests/Rigs ohne Peer bleiben ein reiner No-op.

    Nutzung: `begin()` beim ERSTEN `mouseDrag` (nicht schon im `mouseDown` —
    ein reiner Klick soll den Cursor nicht ausblenden), `end()` im `mouseUp`.
    Beide Aufrufe sind idempotent; der Destruktor des Besitzers sollte
    sicherheitshalber `end()` rufen, falls eine Geste durch Löschen abreißt.
*/
class DragCursorHider
{
public:
    enum class Mode { relative, absolute, crosshair };

    /** Startet das Ausblenden. No-op bei Touch/Pen, ohne Peer, oder wenn
        bereits aktiv (erster Drag gewinnt). */
    void begin (juce::Component& owner, const juce::MouseEvent& event, Mode mode)
    {
        if (active || ! event.source.isMouse() || owner.getPeer() == nullptr)
            return;

        active = true;
        currentMode = mode;
        source = event.source;

        if (currentMode == Mode::relative)
        {
            source->enableUnboundedMouseMovement (true, false);
        }
        else
        {
            target = &owner;
            previousCursor = owner.getMouseCursor();
            owner.setMouseCursor (currentMode == Mode::crosshair
                                      ? juce::MouseCursor::CrosshairCursor
                                      : juce::MouseCursor::NoCursor);
        }
    }

    /** Stellt den Cursor wieder her. Sicher auch ohne vorheriges `begin()`. */
    void end()
    {
        if (! active)
            return;

        active = false;

        if (currentMode == Mode::relative)
        {
            if (source.has_value())
                source->enableUnboundedMouseMovement (false);
        }
        else if (target != nullptr)
        {
            target->setMouseCursor (previousCursor);
            target = nullptr;
        }

        source.reset();
    }

    [[nodiscard]] bool isActive() const noexcept { return active; }

private:
    bool active = false;
    Mode currentMode = Mode::relative;
    juce::Component* target = nullptr;
    juce::MouseCursor previousCursor;
    std::optional<juce::MouseInputSource> source;
};

} // namespace conduit::ui
