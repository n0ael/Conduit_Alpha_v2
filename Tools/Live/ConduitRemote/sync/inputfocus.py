"""Conduit Grid track focus + follow-selection routing (Conduit Block H v2).

Live-performance goal: Conduit's MPE grid and the master MIDI device (Push,
keyboard, ...) play DIFFERENT tracks at the same time, freely switchable.

Focus semantics (set_focus):
  * focus track: input routing = grid_input (Conduit's grid MIDI-out port),
    monitor IN - it always hears Conduit, regardless of arm/selection.
  * every OTHER midi track whose input is "All Ins": monitor OFF ("All Ins"
    includes the Conduit port - Auto would leak grid notes when armed).
    Tracks with an explicit input routing are left alone.
  * Live's currently SELECTED track (midi, on "All Ins", not the focus):
    input = master_input (e.g. "FromPush"), monitor AUTO - remembered as
    the "moved" track so it can be restored later.
  * a PREVIOUS focus track is restored (input back to "All Ins", monitor
    OFF) but only if its input still is the grid port - i.e. only if WE
    set it; user re-routings are never overwritten.

Follow selection (selected_track listener, active only while follow is on
AND a focus exists): the previously moved track is restored to "All Ins" +
monitor OFF (user decision 11.07.2026), the newly selected midi track (on
"All Ins", not the focus) moves to master_input + monitor AUTO.

"All Ins" detection is robust against localisation: entry 0 of
available_input_routing_types IS "All Ins" in Live - matched via
display_name of that first entry, never via a hardcoded string.

All LOM access is guarded (docs/TouchLive.md par.10d fallen); the state
lives only in this script session - a Live restart clears the focus, which
the client observes through the tracks domain going empty on conduit_focus.
"""

import logging

from . import stable_ids

logger = logging.getLogger(__name__)

# Live.Track.Track.monitoring_states: 0 = In, 1 = Auto, 2 = Off
MONITOR_IN = 0
MONITOR_AUTO = 1
MONITOR_OFF = 2


def find_routing_type(track, wanted_name):
    """available_input_routing_types by display_name: exact match first,
    then case-insensitive prefix (Live appends suffixes like " (Port 1)").
    None when nothing matches / routing unavailable."""
    if not wanted_name:
        return None
    try:
        available = list(track.available_input_routing_types)
    except Exception:
        return None
    for routing in available:
        if getattr(routing, "display_name", None) == wanted_name:
            return routing
    lowered = wanted_name.lower()
    for routing in available:
        display = getattr(routing, "display_name", "") or ""
        if display.lower().startswith(lowered):
            return routing
    return None


def apply_track_input(track, monitor_state, input_name):
    """Set a track's monitor + input routing; every sub-operation is
    LOM-defensive, an empty input_name leaves the routing untouched."""
    try:
        track.current_monitoring_state = monitor_state
    except Exception:
        logger.debug("input focus: monitoring not settable on %r",
                     getattr(track, "name", "?"))
    routing = find_routing_type(track, input_name)
    if routing is None:
        return
    try:
        track.input_routing_type = routing
    except Exception:
        logger.debug("input focus: input routing not settable on %r",
                     getattr(track, "name", "?"))


def _all_ins_name(track):
    """display_name of routing entry 0 ("All Ins"), or None."""
    try:
        available = list(track.available_input_routing_types)
    except Exception:
        return None
    if not available:
        return None
    return getattr(available[0], "display_name", None)


def _current_input_name(track):
    try:
        return getattr(track.input_routing_type, "display_name", None)
    except Exception:
        return None


def _is_on_all_ins(track):
    name = _all_ins_name(track)
    return name is not None and _current_input_name(track) == name


def _is_midi(track):
    try:
        return bool(track.has_midi_input)
    except Exception:
        return False


class InputFocusService(object):
    def __init__(self, song):
        self._song = song
        self._focus_key = None    # stable_ids._identity of the focus track
        self._moved_key = None    # identity of the track we moved to master
        self._follow = True
        self._grid_input = ""
        self._master_input = ""
        self._listener_bound = False

        # wired by the manager: tracks domain mark_dirty
        self.on_state_changed = None

    # -- queries (tracks domain) ----------------------------------------------

    def focus_stable_id(self):
        """Stable-ID of the focus track, "" when none / track vanished."""
        track = self._find_by_key(self._focus_key)
        if track is None:
            return ""
        return stable_ids.get_id(track, stable_ids.TRACK_PREFIX)

    def follow_enabled(self):
        return self._follow

    # -- commands ---------------------------------------------------------------

    def set_follow(self, enabled):
        self._follow = bool(enabled)
        self._notify()

    def set_focus(self, target, grid_input, master_input, follow):
        """Route target to the grid (monitor IN + grid_input), silence other
        All-Ins midi tracks, move Live's selection to master_input."""
        if target is None:
            return
        self._grid_input = str(grid_input or "")
        self._master_input = str(master_input or "")
        self._follow = bool(follow)

        song = self._song
        target_key = stable_ids._identity(target)

        # Previous focus: give it back (only if its input still is OUR port)
        previous = self._find_by_key(self._focus_key)
        if previous is not None and self._focus_key != target_key:
            self._restore_to_all_ins(previous, only_if_input=self._grid_input)

        self._focus_key = target_key
        self._moved_key = None
        self._ensure_listener()

        selected = self._selected_track()
        selected_key = (stable_ids._identity(selected)
                        if selected is not None else None)

        for track in song.tracks:
            if not _is_midi(track):
                continue
            key = stable_ids._identity(track)
            if key == target_key:
                apply_track_input(track, MONITOR_IN, self._grid_input)
            elif key == selected_key and _is_on_all_ins(track):
                apply_track_input(track, MONITOR_AUTO, self._master_input)
                self._moved_key = key
            elif _is_on_all_ins(track):
                apply_track_input(track, MONITOR_OFF, "")
        self._notify()

    # -- follow selection ---------------------------------------------------------

    def on_selection_changed(self):
        """selected_track listener body (also callable from tests)."""
        if not self._follow or self._focus_key is None:
            return

        selected = self._selected_track()
        selected_key = (stable_ids._identity(selected)
                        if selected is not None else None)

        if selected_key == self._moved_key:
            return   # nothing changed for us

        # Restore the previously moved track: All Ins + monitor OFF
        moved = self._find_by_key(self._moved_key)
        if moved is not None and self._moved_key != self._focus_key:
            self._restore_to_all_ins(moved, only_if_input=self._master_input)
        self._moved_key = None

        # Move the new selection out of All Ins (unless it is the focus)
        if (selected is not None and selected_key != self._focus_key
                and _is_midi(selected) and _is_on_all_ins(selected)):
            apply_track_input(selected, MONITOR_AUTO, self._master_input)
            self._moved_key = selected_key
        self._notify()

    # -- lifecycle ---------------------------------------------------------------

    def detach(self):
        if not self._listener_bound:
            return
        try:
            self._song.view.remove_selected_track_listener(
                self.on_selection_changed)
        except Exception:
            logger.exception("input focus: listener unbind failed")
        self._listener_bound = False

    # -- internals ---------------------------------------------------------------

    def _ensure_listener(self):
        if self._listener_bound:
            return
        try:
            self._song.view.add_selected_track_listener(
                self.on_selection_changed)
            self._listener_bound = True
        except Exception:
            # Kein Listener = kein Follow (Fokus-Command funktioniert
            # trotzdem); Muster Domain.attach-Poll-Fallback, hier ohne Poll.
            logger.exception("input focus: selection listener unavailable")

    def _selected_track(self):
        try:
            return self._song.view.selected_track
        except Exception:
            return None

    def _find_by_key(self, key):
        if key is None:
            return None
        try:
            tracks = list(self._song.tracks)
        except Exception:
            return None
        for track in tracks:
            if stable_ids._identity(track) == key:
                return track
        return None

    def _restore_to_all_ins(self, track, only_if_input):
        """Back to "All Ins" + monitor OFF - but never overwrite an input
        the USER changed meanwhile (current input must still be ours)."""
        current = _current_input_name(track)
        if only_if_input and current is not None:
            if not (current == only_if_input
                    or current.lower().startswith(only_if_input.lower())):
                return
        all_ins = _all_ins_name(track)
        if all_ins is None:
            return
        apply_track_input(track, MONITOR_OFF, all_ins)

    def _notify(self):
        if self.on_state_changed is not None:
            try:
                self.on_state_changed()
            except Exception:
                logger.exception("input focus: state notify failed")
