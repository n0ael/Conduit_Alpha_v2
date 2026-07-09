"""Stub of the Live Object Model subset used by ConduitRemote.

Mimics LOM semantics closely enough for unit tests:
- properties fire registered listeners on change (like LOM add_*_listener)
- tracks/scenes/clip_slots are plain lists that tests may mutate, followed
  by firing the matching structural listener
- DeviceParameter has value/min/max and value listeners

No Live import anywhere - tests run under plain CPython.
"""


class _Listenable(object):
    """Provides add_/remove_<prop>_listener plumbing."""

    def __init__(self):
        self.__dict__["_listeners"] = {}

    def _listeners_for(self, prop):
        return self.__dict__["_listeners"].setdefault(prop, [])

    def add_listener(self, prop, callback):
        self._listeners_for(prop).append(callback)

    def remove_listener(self, prop, callback):
        self._listeners_for(prop).remove(callback)

    def notify(self, prop):
        for callback in list(self._listeners_for(prop)):
            callback()

    def __getattr__(self, name):
        # synthesize add_/remove_x_listener methods, LOM-style
        if name.startswith("add_") and name.endswith("_listener"):
            prop = name[4:-9]
            return lambda cb: self.add_listener(prop, cb)
        if name.startswith("remove_") and name.endswith("_listener"):
            prop = name[7:-9]
            return lambda cb: self.remove_listener(prop, cb)
        raise AttributeError(name)

    def _set(self, prop, value):
        """Set attribute and fire listeners if changed."""
        old = self.__dict__.get(prop)
        self.__dict__[prop] = value
        if old != value:
            self.notify(prop)


class DeviceParameter(_Listenable):
    def __init__(self, name, value=0.0, min_value=0.0, max_value=1.0):
        _Listenable.__init__(self)
        self.__dict__["name"] = name
        self.__dict__["min"] = min_value
        self.__dict__["max"] = max_value
        self.__dict__["is_quantized"] = False
        self.__dict__["value"] = value

    def __setattr__(self, key, value):
        if key == "value":
            lo, hi = self.__dict__["min"], self.__dict__["max"]
            value = max(lo, min(hi, float(value)))
            self._set("value", value)
        else:
            self.__dict__[key] = value

    def __str__(self):
        return "%.2f" % self.value


class MixerDevice(object):
    def __init__(self, num_sends=2):
        self.volume = DeviceParameter("Volume", 0.85)
        self.panning = DeviceParameter("Pan", 0.0, -1.0, 1.0)
        self.sends = [DeviceParameter("Send %d" % i, 0.0)
                      for i in range(num_sends)]


class Clip(_Listenable):
    def __init__(self, name="", color=0):
        _Listenable.__init__(self)
        self.__dict__["name"] = name
        self.__dict__["color"] = color
        self.__dict__["is_playing"] = False
        self.__dict__["is_recording"] = False
        self.__dict__["is_triggered"] = False

    def __setattr__(self, key, value):
        self._set(key, value)


class ClipSlot(_Listenable):
    def __init__(self, clip=None):
        _Listenable.__init__(self)
        self.__dict__["clip"] = clip
        self.__dict__["is_triggered"] = False
        self.__dict__["playing_status"] = 0

    @property
    def has_clip(self):
        return self.clip is not None

    def fire(self):
        if self.clip is not None:
            self.clip.is_triggered = True
        self.__dict__["fired"] = True   # test hook

    def stop(self):
        if self.clip is not None:
            self.clip.is_playing = False

    def set_clip(self, clip):
        self._set("clip", clip)
        self.notify("has_clip")

    def __setattr__(self, key, value):
        self._set(key, value)


class Scene(_Listenable):
    def __init__(self, name=""):
        _Listenable.__init__(self)
        self.__dict__["name"] = name
        self.__dict__["color"] = 0
        self.__dict__["is_triggered"] = False

    def fire(self):
        self.__dict__["fired"] = True   # test hook

    def __setattr__(self, key, value):
        self._set(key, value)


class Track(_Listenable):
    """Live-Realismus (Befund Feldtest 09.07.2026): der Zugriff auf `arm`
    (und die arm-Listener) wirft im echten Live eine RuntimeError, wenn der
    Track nicht armbar ist (Returns/Master); Master wirft zusätzlich bei
    `mute`/`solo`. Der Stub bildet das nach, damit die Domains die Guards
    beweisen müssen."""

    def __init__(self, name, color=0xFF0000, num_slots=8, num_sends=2,
                 has_midi_input=False, can_be_armed=True, has_mute_solo=True):
        _Listenable.__init__(self)
        self.__dict__["name"] = name
        self.__dict__["color"] = color
        self.__dict__["can_be_armed"] = can_be_armed
        self.__dict__["has_mute_solo"] = has_mute_solo
        if has_mute_solo:
            self.__dict__["mute"] = False
            self.__dict__["solo"] = False
        if can_be_armed:
            self.__dict__["arm"] = False
        self.__dict__["has_midi_input"] = has_midi_input
        self.__dict__["has_audio_output"] = True
        self.__dict__["is_foldable"] = False
        self.__dict__["output_meter_left"] = 0.0
        self.__dict__["output_meter_right"] = 0.0
        self.mixer_device = MixerDevice(num_sends)
        self.clip_slots = [ClipSlot() for _ in range(num_slots)]
        self.devices = []

    def _forbidden(self, name):
        if name in ("arm", "add_arm_listener", "remove_arm_listener"):
            return not self.__dict__["can_be_armed"]
        if name in ("mute", "solo", "add_mute_listener", "remove_mute_listener",
                    "add_solo_listener", "remove_solo_listener"):
            return not self.__dict__["has_mute_solo"]
        return False

    def __getattr__(self, name):
        if self._forbidden(name):
            raise RuntimeError("Track %r does not support %s (LOM)"
                               % (self.__dict__["name"], name))
        return _Listenable.__getattr__(self, name)

    def stop_all_clips(self):
        self.__dict__["stopped_all"] = True   # test hook
        for slot in self.clip_slots:
            if slot.has_clip:
                slot.clip.is_playing = False

    def __setattr__(self, key, value):
        if self._forbidden(key):
            raise RuntimeError("Track %r does not support %s (LOM)"
                               % (self.__dict__["name"], key))
        if key in ("mixer_device", "clip_slots", "devices"):
            self.__dict__[key] = value
        else:
            self._set(key, value)


class Song(_Listenable):
    def __init__(self, num_tracks=2, num_scenes=8, num_sends=2):
        _Listenable.__init__(self)
        self.__dict__["tempo"] = 120.0
        self.__dict__["is_playing"] = False
        self.__dict__["metronome"] = False
        self.__dict__["session_record"] = False
        self.__dict__["current_song_time"] = 0.0
        self.__dict__["signature_numerator"] = 4
        self.__dict__["signature_denominator"] = 4
        self.tracks = [Track("Track %d" % (i + 1), num_slots=num_scenes,
                             num_sends=num_sends)
                       for i in range(num_tracks)]
        # Returns: mute/solo ja, arm nein; Master: nichts davon (wie Live)
        self.return_tracks = [Track("Return %s" % chr(65 + i), num_slots=0,
                                    num_sends=0, can_be_armed=False)
                              for i in range(num_sends)]
        self.master_track = Track("Master", num_slots=0, num_sends=0,
                                  can_be_armed=False, has_mute_solo=False)
        self.scenes = [Scene("Scene %d" % (i + 1)) for i in range(num_scenes)]

    # transport methods (LOM names)
    def start_playing(self):
        self.is_playing = True

    def stop_playing(self):
        self.is_playing = False

    def undo(self):
        self.__dict__["did_undo"] = True   # test hook

    def redo(self):
        self.__dict__["did_redo"] = True   # test hook

    def __setattr__(self, key, value):
        if key in ("tracks", "return_tracks", "master_track", "scenes"):
            self.__dict__[key] = value
        else:
            self._set(key, value)


class FakeSender(object):
    """Captures everything a Domain sends; used across the test suite."""

    def __init__(self):
        self.sent = []   # list of (address, seq, payload_dict)

    def send_json(self, address, seq, payload, force=False):
        self.sent.append((address, seq, payload))

    def last(self):
        return self.sent[-1] if self.sent else None

    def clear(self):
        del self.sent[:]
