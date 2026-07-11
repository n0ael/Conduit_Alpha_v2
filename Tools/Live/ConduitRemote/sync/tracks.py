"""Tracks domain: track/return/master STRUCTURE only (name/color/kind/index).

Mixer values (volume/pan/sends/mute/solo/arm) live in sync.mixer.MixerDomain
and share the exact same stable ids (see sync.stable_ids) so a client can
join structure and mixer state on the same key.
"""

from .base import Domain
from . import stable_ids


class TracksDomain(Domain):
    NAME = "tracks"

    def __init__(self, live_song, sender):
        Domain.__init__(self, live_song, sender)
        self._structure_bound = False
        self._view_bound = False
        self._track_bindings = []   # list of (track, name_cb, color_cb)
        # Conduit Block H v2: vom Manager injizierter InputFocusService --
        # liefert conduit_focus fuer collect() und markiert per
        # on_state_changed die Domain dirty. None ausserhalb des Managers.
        self.input_focus = None

    # -- snapshot -------------------------------------------------------------

    def collect(self):
        song = self.song
        result = {}
        for index, track in enumerate(song.tracks):
            sid = stable_ids.get_id(track, stable_ids.TRACK_PREFIX)
            result[sid] = {
                "name": track.name,
                "color": track.color,
                "kind": "midi" if track.has_midi_input else "audio",
                "index": index,
            }
        # Block H v2 (Conduit Grid-Track-Selector): Lives Selektion, der
        # Conduit-Fokus-Track und die verfuegbaren MIDI-From-Namen (fuers
        # Master-MIDI-Dropdown) reisen als Skalar-Keys mit.
        result["selected"] = self._selected_stable_id()
        result["conduit_focus"] = (self.input_focus.focus_stable_id()
                                   if self.input_focus is not None else "")
        result["input_options"] = self._input_options()
        for index, track in enumerate(song.return_tracks):
            sid = stable_ids.get_id(track, stable_ids.RETURN_PREFIX)
            result[sid] = {
                "name": track.name,
                "color": track.color,
                "kind": "return",
                "index": index,
            }
        master = song.master_track
        sid = stable_ids.get_id(master, stable_ids.MASTER_PREFIX)
        result[sid] = {
            "name": master.name,
            "color": master.color,
            "kind": "master",
            "index": 0,
        }
        return result

    def _selected_stable_id(self):
        """Stable-ID von Lives selektiertem Track -- NUR regulaere Tracks
        (fuer Returns/Master keine tr:-ID erfinden), leerer String sonst."""
        try:
            selected = self.song.view.selected_track
        except Exception:
            return ""
        if selected is None:
            return ""
        key = stable_ids._identity(selected)
        for track in self.song.tracks:
            if stable_ids._identity(track) == key:
                return stable_ids.get_id(track, stable_ids.TRACK_PREFIX)
        return ""

    def _input_options(self):
        """display_names der Input-Routing-Typen des ersten regulaeren
        Tracks (Live-weit identische Liste) -- LOM-defensiv, ggf. leer."""
        try:
            tracks = list(self.song.tracks)
        except Exception:
            return []
        for track in tracks:
            try:
                available = list(track.available_input_routing_types)
            except Exception:
                continue
            names = []
            for routing in available:
                name = getattr(routing, "display_name", None)
                if name:
                    names.append(str(name))
            return names
        return []

    # -- listener plumbing ------------------------------------------------------

    def _all_tracks(self):
        song = self.song
        tracks = list(song.tracks) + list(song.return_tracks)
        tracks.append(song.master_track)
        return tracks

    def _on_structure_change(self):
        self._rebind_track_listeners()
        self.mark_dirty()

    def _on_field_change(self):
        self.mark_dirty()

    def _rebind_track_listeners(self):
        for track, name_cb, color_cb in self._track_bindings:
            try:
                track.remove_name_listener(name_cb)
                track.remove_color_listener(color_cb)
            except Exception:
                pass   # Track existiert nicht mehr (Reorder/Delete/Teardown)
        self._track_bindings = []
        for track in self._all_tracks():
            name_cb = self._on_field_change
            color_cb = self._on_field_change
            track.add_name_listener(name_cb)
            track.add_color_listener(color_cb)
            self._track_bindings.append((track, name_cb, color_cb))

    def on_attach(self):
        if self._structure_bound:
            return
        song = self.song
        song.add_tracks_listener(self._on_structure_change)
        song.add_return_tracks_listener(self._on_structure_change)
        self._rebind_track_listeners()
        self._structure_bound = True
        # Selektions-Listener EINZELN geguarded (Block H v2): scheitert nur
        # er, soll die Domain nicht komplett in den Poll-Fallback kippen --
        # das selected-Feld heilt dann ueber Struktur-/Feld-Diffs.
        try:
            song.view.add_selected_track_listener(self._on_field_change)
            self._view_bound = True
        except Exception:
            self._view_bound = False

    def on_detach(self):
        if not self._structure_bound:
            return
        song = self.song
        if self._view_bound:
            try:
                song.view.remove_selected_track_listener(self._on_field_change)
            except Exception:
                pass   # View/LOM beim Teardown schon tot
            self._view_bound = False
        song.remove_tracks_listener(self._on_structure_change)
        song.remove_return_tracks_listener(self._on_structure_change)
        for track, name_cb, color_cb in self._track_bindings:
            try:
                track.remove_name_listener(name_cb)
                track.remove_color_listener(color_cb)
            except Exception:
                pass   # LOM-Objekt beim Teardown schon tot (Log 09.07.2026)
        self._track_bindings = []
        self._structure_bound = False
