"""Mixer domain: volume/pan/sends/mute/solo/arm for tracks/returns/master.

Uses the SAME stable ids as sync.tracks.TracksDomain (same underlying Live
objects, same prefixes from sync.stable_ids) so a client can join a track's
structure entry and its mixer entry on one key.

Design choice for the master track: real Live's master track has no mute/
solo/arm and no sends of its own (only volume/pan are meaningful), so its
mixer entry OMITS the "sends"/"mute"/"solo"/"arm" keys entirely rather than
padding them with placeholder values a client might mistake for real state.
Regular tracks and return tracks both get the full key set.
"""

from .base import Domain
from . import stable_ids


class MixerDomain(Domain):
    NAME = "mixer"

    def __init__(self, live_song, sender):
        Domain.__init__(self, live_song, sender)
        self._structure_bound = False
        self._track_bindings = []   # list of _TrackMixerBinding

    # -- snapshot -------------------------------------------------------------

    def collect(self):
        song = self.song
        result = {}
        for track in song.tracks:
            sid = stable_ids.get_id(track, stable_ids.TRACK_PREFIX)
            result[sid] = _full_state(track)
        for track in song.return_tracks:
            sid = stable_ids.get_id(track, stable_ids.RETURN_PREFIX)
            result[sid] = _full_state(track)
        master = song.master_track
        sid = stable_ids.get_id(master, stable_ids.MASTER_PREFIX)
        result[sid] = _master_state(master)
        return result

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
        for binding in self._track_bindings:
            binding.unbind()
        self._track_bindings = []
        for track in self._all_tracks():
            binding = _TrackMixerBinding(track, self._on_field_change)
            binding.bind()
            self._track_bindings.append(binding)

    def on_attach(self):
        if self._structure_bound:
            return
        song = self.song
        song.add_tracks_listener(self._on_structure_change)
        song.add_return_tracks_listener(self._on_structure_change)
        self._rebind_track_listeners()
        self._structure_bound = True

    def on_detach(self):
        if not self._structure_bound:
            return
        song = self.song
        song.remove_tracks_listener(self._on_structure_change)
        song.remove_return_tracks_listener(self._on_structure_change)
        for binding in self._track_bindings:
            binding.unbind()
        self._track_bindings = []
        self._structure_bound = False


def _full_state(track):
    """Volle Mixer-Sicht eines Tracks — LOM-sicher: `arm` existiert nur auf
    armbaren Tracks, `mute`/`solo` nicht auf dem Master; der Zugriff WIRFT
    im echten Live (RuntimeError), statt zu fehlen (Feldtest 09.07.2026 —
    die Domain starb daran bei jedem collect()). Fehlende Faehigkeiten
    lassen den Key komplett weg (wie _master_state), der Client blendet
    die Regler nach Key-Existenz aus."""
    md = track.mixer_device
    state = {
        "vol": float(md.volume.value),
        "pan": float(md.panning.value),
        "sends": [float(p.value) for p in md.sends],
    }
    for prop in ("mute", "solo"):
        try:
            state[prop] = bool(getattr(track, prop))
        except Exception:
            pass
    if getattr(track, "can_be_armed", False):
        try:
            state["arm"] = bool(track.arm)
        except Exception:
            pass
    return state


def _master_state(track):
    md = track.mixer_device
    return {
        "vol": float(md.volume.value),
        "pan": float(md.panning.value),
    }


class _TrackMixerBinding(object):
    """Binds volume/panning/sends/mute/solo/arm value listeners on one
    track's mixer device.

    LOM-sicher (Feldtest 09.07.2026): add_arm_listener wirft auf nicht
    armbaren Tracks, add_mute/solo_listener auf dem Master — jede Bindung
    laeuft deshalb einzeln durch try/except und wird fuer unbind()
    protokolliert. Listener sind nur change hints (mark_dirty); collect()
    bleibt die Wahrheit."""

    def __init__(self, track, on_change):
        self.track = track
        self.on_change = on_change
        self._bound_params = []   # DeviceParameter mit value-Listener
        self._bound_props = []    # Property-Namen mit gebundenem Listener

    def bind(self):
        track = self.track
        md = track.mixer_device

        for param in [md.volume, md.panning] + list(md.sends):
            param.add_value_listener(self.on_change)
            self._bound_params.append(param)

        props = ["mute", "solo"]
        if getattr(track, "can_be_armed", False):
            props.append("arm")

        for prop in props:
            try:
                getattr(track, "add_%s_listener" % prop)(self.on_change)
                self._bound_props.append(prop)
            except Exception:
                pass   # Faehigkeit fehlt (z. B. Master) — collect() laesst den Key weg

    def unbind(self):
        for param in self._bound_params:
            try:
                param.remove_value_listener(self.on_change)
            except Exception:
                pass
        self._bound_params = []

        for prop in self._bound_props:
            try:
                getattr(self.track, "remove_%s_listener" % prop)(self.on_change)
            except Exception:
                pass
        self._bound_props = []
