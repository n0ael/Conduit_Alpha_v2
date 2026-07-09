"""High-rate meter path (M2, TOUCHLIVE.md par.2 "Meter/Peaks").

Deliberately NOT a Domain: meters change every tick, so diffing/JSON would
be pure overhead, and a lost datagram needs no healing (the next frame
overwrites everything). One compact OSC message per manager tick:

    /remote/meters  [id:str, left:float, right:float] * n   (flat triplets)

covering regular tracks, return tracks and the master track, using the
SAME stable ids as the tracks/mixer domains so the client joins meters
onto its channel strips by key. No seq number - frames are idempotent and
a late datagram at ~10 Hz is visually irrelevant.

Values are Live's raw ``output_meter_left/right`` norm (0..1, Live's own
meter ballistics) - display mapping is the client's business.

Silence dedupe: once a frame is all-zero AND the previous sent frame was
all-zero too, nothing goes out until any level rises again - an idle Live
set costs no bandwidth (the one zero frame lets the client's meters fall
to rest).

Rate: every METER_TICK_DIVIDER-th manager tick (~10 Hz at divider 1).
Subscription via /remote/meters/subscribe|unsubscribe, torn down by the
heartbeat timeout like the domains (manager wiring).
"""

from . import stable_ids
from .. import config

_SILENCE_EPS = 1e-4


class MeterStream(object):
    def __init__(self, live_song, osc_send, tick_divider=None):
        """osc_send: callable(address, args_list) - OscServer.send or a
        test capture."""
        self.song = live_song
        self.osc_send = osc_send
        self.subscribed = False
        self._divider = (config.METER_TICK_DIVIDER if tick_divider is None
                         else tick_divider)
        self._last_was_silent = False

    # -- protocol entry points (Manager) ------------------------------------

    def on_subscribe(self):
        self.subscribed = True
        self._last_was_silent = False   # erster Frame geht immer raus

    def on_unsubscribe(self):
        self.subscribed = False

    def on_tick(self, tick_index):
        if not self.subscribed:
            return
        if self._divider > 1 and tick_index % self._divider:
            return

        args, silent = self._collect()

        if silent and self._last_was_silent:
            return   # Stille-Dedupe: ein Null-Frame wurde bereits gesendet
        self._last_was_silent = silent

        self.osc_send("/remote/meters", args)

    # -- internals -----------------------------------------------------------

    def _collect(self):
        song = self.song
        args = []
        silent = True

        groups = ((song.tracks, stable_ids.TRACK_PREFIX),
                  (song.return_tracks, stable_ids.RETURN_PREFIX),
                  ((song.master_track,), stable_ids.MASTER_PREFIX))

        for tracks, prefix in groups:
            for track in tracks:
                left, right = _meter_pair(track)
                if left > _SILENCE_EPS or right > _SILENCE_EPS:
                    silent = False
                args.extend([stable_ids.get_id(track, prefix), left, right])

        return args, silent


def _meter_pair(track):
    """LOM-sicher: output_meter_* kann auf Tracks ohne Audio-Ausgang
    WERFEN (nicht nur fehlen) — dann 0/0 statt Domain-Absturz."""
    try:
        return (float(track.output_meter_left), float(track.output_meter_right))
    except Exception:
        return (0.0, 0.0)
