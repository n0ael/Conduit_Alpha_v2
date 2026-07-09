"""Devices domain (M3): top-level device chains + generic parameters.

SHAPE (same shallow-diff lesson as session.py - compute_diff() resends a
whole top-level value when it changes, so granularity IS the key layout):

    "chain:{tid}"   -> [dvid, ...]          device chain per track
                                            (tracks + returns + master)
    "dev:{dvid}"    -> {name, class_name, is_active}
    "parmeta:{dvid}" -> [{name, min, max, quant, items?}, ...]  (static)
    "parvals:{dvid}" -> [value, ...]        the HOT row: one parameter
                                            change only resends this small
                                            float array, never the metadata

parameters[0] is Live's "Device On" - included verbatim; the client's bank
UI starts at index 1 and uses /live/device/set/is_active for the switch.

M3 scope: TOP-LEVEL devices only (no rack/chain recursion - that lands
with the bespoke work, TOUCHLIVE.md par.6b). LOM guards throughout: every
capability read is defensive, listener binding failures fall back to the
Domain polling fallback (base.attach).
"""

from .base import Domain
from . import stable_ids


def _chain_tracks(song):
    """(track, prefix) fuer alle Ketten-Traeger: Tracks, Returns, Master."""
    for track in song.tracks:
        yield track, stable_ids.TRACK_PREFIX
    for track in song.return_tracks:
        yield track, stable_ids.RETURN_PREFIX
    yield song.master_track, stable_ids.MASTER_PREFIX


def _devices_of(track):
    try:
        return list(track.devices)
    except Exception:
        return []


def _param_meta(param):
    meta = {
        "name": str(getattr(param, "name", "")),
        "min": float(getattr(param, "min", 0.0)),
        "max": float(getattr(param, "max", 1.0)),
        "quant": bool(getattr(param, "is_quantized", False)),
    }
    if meta["quant"]:
        try:
            meta["items"] = [str(item) for item in param.value_items]
        except Exception:
            pass
    return meta


class DevicesDomain(Domain):
    NAME = "devices"

    def __init__(self, live_song, sender):
        Domain.__init__(self, live_song, sender)
        self._structure_bound = False
        self._device_bindings = []   # (device, [callbacks...]) fuer unbind
        self._param_bindings = []    # (param, callback)

    # -- snapshot -------------------------------------------------------------

    def collect(self):
        result = {}

        for track, prefix in _chain_tracks(self.song):
            tid = stable_ids.get_id(track, prefix)
            chain = []

            for device in _devices_of(track):
                dvid = stable_ids.get_id(device, stable_ids.DEVICE_PREFIX)
                chain.append(dvid)

                params = list(getattr(device, "parameters", []) or [])
                result["dev:" + dvid] = {
                    "name": str(getattr(device, "name", "")),
                    "class_name": str(getattr(device, "class_name", "")),
                    "is_active": bool(getattr(device, "is_active", True)),
                }
                result["parmeta:" + dvid] = [_param_meta(p) for p in params]
                result["parvals:" + dvid] = [
                    float(getattr(p, "value", 0.0)) for p in params]

            result["chain:" + tid] = chain

        return result

    # -- listener plumbing ------------------------------------------------------

    def _on_structure_change(self):
        self._rebind_device_listeners()
        self.mark_dirty()

    def _on_field_change(self):
        self.mark_dirty()

    def _rebind_device_listeners(self):
        self._unbind_device_listeners()

        for track, _prefix in _chain_tracks(self.song):
            for device in _devices_of(track):
                bound = []

                for prop in ("name", "is_active", "parameters"):
                    try:
                        getattr(device, "add_%s_listener" % prop)(
                            self._on_field_change)
                        bound.append(prop)
                    except Exception:
                        pass

                self._device_bindings.append((device, bound))

                for param in list(getattr(device, "parameters", []) or []):
                    try:
                        param.add_value_listener(self._on_field_change)
                        self._param_bindings.append(param)
                    except Exception:
                        pass

    def _unbind_device_listeners(self):
        for device, bound in self._device_bindings:
            for prop in bound:
                try:
                    getattr(device, "remove_%s_listener" % prop)(
                        self._on_field_change)
                except Exception:
                    pass   # Device existiert nicht mehr (Delete/Teardown)
        self._device_bindings = []

        for param in self._param_bindings:
            try:
                param.remove_value_listener(self._on_field_change)
            except Exception:
                pass
        self._param_bindings = []

    def on_attach(self):
        if self._structure_bound:
            return
        song = self.song
        song.add_tracks_listener(self._on_structure_change)
        song.add_return_tracks_listener(self._on_structure_change)

        for track, _prefix in _chain_tracks(song):
            try:
                track.add_devices_listener(self._on_structure_change)
            except Exception:
                pass

        self._rebind_device_listeners()
        self._structure_bound = True

    def on_detach(self):
        if not self._structure_bound:
            return
        song = self.song
        try:
            song.remove_tracks_listener(self._on_structure_change)
            song.remove_return_tracks_listener(self._on_structure_change)
        except Exception:
            pass

        for track, _prefix in _chain_tracks(song):
            try:
                track.remove_devices_listener(self._on_structure_change)
            except Exception:
                pass

        self._unbind_device_listeners()
        self._structure_bound = False
