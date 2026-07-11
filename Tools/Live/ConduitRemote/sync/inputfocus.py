"""Conduit Grid track focus routing (Conduit Block H v2, rev5).

Live-performance goal: Conduit's MPE grid and the master MIDI device (Push,
keyboard, ...) play DIFFERENT tracks at the same time, freely switchable.

Final semantics (User-Entscheidung 11.07.2026 abends -- STATISCH statt
Selektions-Following; Lives eigene Arm-/Selektions-Mechanik uebernimmt):

  * focus track: input = grid_input (Conduit's grid MIDI-out port),
    monitor IN - it always hears Conduit, regardless of arm/selection.
  * previous focus track (input still on grid_input): given back to
    master_input + monitor AUTO (plays normally with Push again).
  * every other midi track on "All Ins": input = master_input (e.g.
    "FromPush") - the monitor state is left alone (only a stale OFF from
    the earlier follow-implementation is healed back to AUTO once).
    "All Ins" would include the Conduit port, so these tracks would leak
    grid notes when armed - explicit master routing fixes that for good.
  * any OTHER input (sequencer, hardware, ...) is NEVER touched, neither
    input nor monitor (User-Regel: ein sequencer-gespeister Track darf
    nichts verlieren).

No selection listener, no polling, no monitor juggling - the pass runs
once per set_focus and is idempotent (writes only on actual change).

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


def set_monitor(track, monitor_state):
    """LOM-defensiv; schreibt nur bei echter Aenderung (keine No-op-Writes
    in Lives Undo-/Listener-Maschinerie)."""
    try:
        if track.current_monitoring_state != monitor_state:
            track.current_monitoring_state = monitor_state
    except Exception:
        logger.debug("input focus: monitoring not settable on %r",
                     getattr(track, "name", "?"))


def set_input(track, input_name):
    """Input-Routing per Name setzen (leer = unangetastet), LOM-defensiv,
    Write nur bei Aenderung."""
    routing = find_routing_type(track, input_name)
    if routing is None:
        return
    try:
        wanted = getattr(routing, "display_name", None)
        if _current_input_name(track) != wanted:
            track.input_routing_type = routing
    except Exception:
        logger.debug("input focus: input routing not settable on %r",
                     getattr(track, "name", "?"))


def apply_track_input(track, monitor_state, input_name):
    set_monitor(track, monitor_state)
    set_input(track, input_name)


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


def _current_monitor(track):
    try:
        return track.current_monitoring_state
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
        self._grid_input = ""
        self._master_input = ""

        # wired by the manager: tracks domain mark_dirty
        self.on_state_changed = None

    # -- queries (tracks domain) ----------------------------------------------

    def focus_stable_id(self):
        """Stable-ID of the focus track, "" when none / track vanished."""
        track = self._find_by_key(self._focus_key)
        if track is None:
            return ""
        return stable_ids.get_id(track, stable_ids.TRACK_PREFIX)

    # -- commands ---------------------------------------------------------------

    def set_focus(self, target, grid_input, master_input):
        """Route target to the grid, move every All-Ins midi track to the
        master input (statische Aufteilung, rev5)."""
        if target is None:
            return
        self._grid_input = str(grid_input or "")
        self._master_input = str(master_input or "")
        self._focus_key = stable_ids._identity(target)

        logger.info("input focus rev5: focus=%s grid=%r master=%r",
                    self.focus_stable_id(), self._grid_input,
                    self._master_input)

        self._apply_routing()
        self._notify()

    def _apply_routing(self):
        """One idempotent pass over song.tracks (semantics: module docs)."""
        if self._focus_key is None:
            return

        try:
            tracks = list(self._song.tracks)
        except Exception:
            return

        for track in tracks:
            if not _is_midi(track):
                continue
            key = stable_ids._identity(track)
            if key == self._focus_key:
                apply_track_input(track, MONITOR_IN, self._grid_input)
            elif self._input_matches(track, self._grid_input):
                # Ex-Fokus: zurueck ans Master-Device, wieder normal spielbar
                apply_track_input(track, MONITOR_AUTO, self._master_input)
            elif _is_on_all_ins(track):
                # Statische Umstellung: Input -> Master; Monitor bleibt Sache
                # des Users -- nur ein stale OFF der frueheren
                # Follow-Implementierung wird einmalig auf AUTO geheilt.
                if _current_monitor(track) == MONITOR_OFF:
                    set_monitor(track, MONITOR_AUTO)
                set_input(track, self._master_input)
            # jeder andere Input: komplett tabu (Sequencer/Hardware-Regel)

    # -- lifecycle ---------------------------------------------------------------

    def detach(self):
        pass   # rev5: kein Listener mehr -- Hook fuer den Manager-Teardown

    # -- internals ---------------------------------------------------------------

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

    def _input_matches(self, track, wanted_name):
        """IST-Input des Tracks == wanted_name (exakt oder Praefix, wie das
        Routing-Matching)? False bei leerem Namen/unlesbarem Routing."""
        if not wanted_name:
            return False
        current = _current_input_name(track)
        if current is None:
            return False
        return (current == wanted_name
                or current.lower().startswith(wanted_name.lower()))

    def _notify(self):
        if self.on_state_changed is not None:
            try:
                self.on_state_changed()
            except Exception:
                logger.exception("input focus: state notify failed")
