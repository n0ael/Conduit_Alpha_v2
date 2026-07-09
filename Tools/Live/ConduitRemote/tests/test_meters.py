"""MeterStream (M2): subscription, frame format, silence dedupe."""

from ConduitRemote.sync import stable_ids
from ConduitRemote.sync.meters import MeterStream
from ConduitRemote.tests.stub_live import Song


class CaptureSend(object):
    def __init__(self):
        self.sent = []   # list of (address, args)

    def __call__(self, address, args):
        self.sent.append((address, list(args)))


def make_stream(num_tracks=2, num_sends=2):
    stable_ids.clear()
    song = Song(num_tracks=num_tracks, num_scenes=2, num_sends=num_sends)
    send = CaptureSend()
    stream = MeterStream(song, send, tick_divider=1)
    return song, send, stream


def test_unsubscribed_sends_nothing():
    _song, send, stream = make_stream()
    stream.on_tick(1)
    assert send.sent == []


def test_frame_covers_tracks_returns_master_as_flat_triplets():
    song, send, stream = make_stream(num_tracks=2, num_sends=2)
    song.tracks[0].output_meter_left = 0.5
    song.tracks[0].output_meter_right = 0.25

    stream.on_subscribe()
    stream.on_tick(1)

    assert len(send.sent) == 1
    address, args = send.sent[0]
    assert address == "/remote/meters"

    # 2 Tracks + 2 Returns + Master = 5 Tripel
    assert len(args) == 5 * 3

    triplets = {args[i]: (args[i + 1], args[i + 2])
                for i in range(0, len(args), 3)}

    tr0 = stable_ids.get_id(song.tracks[0], stable_ids.TRACK_PREFIX)
    ma = stable_ids.get_id(song.master_track, stable_ids.MASTER_PREFIX)
    assert triplets[tr0] == (0.5, 0.25)
    assert triplets[ma] == (0.0, 0.0)

    # Stable-IDs stimmen mit der tracks-Domain überein (gleiche Registry)
    assert tr0.startswith("tr:")
    assert ma.startswith("ma:")


def test_silence_dedupe_sends_one_zero_frame_then_stops():
    song, send, stream = make_stream()
    stream.on_subscribe()

    stream.on_tick(1)   # alles 0 → EIN Null-Frame (Client fällt auf Ruhe)
    stream.on_tick(2)
    stream.on_tick(3)
    assert len(send.sent) == 1

    song.tracks[1].output_meter_right = 0.8
    stream.on_tick(4)
    assert len(send.sent) == 2

    song.tracks[1].output_meter_right = 0.0
    stream.on_tick(5)   # zurück zur Stille → genau ein weiterer Null-Frame
    stream.on_tick(6)
    assert len(send.sent) == 3


def test_unsubscribe_stops_and_resubscribe_resends_even_when_silent():
    _song, send, stream = make_stream()
    stream.on_subscribe()
    stream.on_tick(1)
    stream.on_unsubscribe()
    stream.on_tick(2)
    assert len(send.sent) == 1

    stream.on_subscribe()   # Re-Subscribe reißt den Stille-Latch auf
    stream.on_tick(3)
    assert len(send.sent) == 2


def test_tick_divider_thins_the_rate():
    song, send, stream = make_stream()
    stream = MeterStream(song, send, tick_divider=3)
    stream.on_subscribe()
    song.tracks[0].output_meter_left = 0.4

    for tick in range(1, 10):
        stream.on_tick(tick)

    # nur Ticks 3, 6, 9
    assert len(send.sent) == 3


def test_gain_reduction_devices_append_dv_triplets():
    """Compressor & Co. (gain_reduction vorhanden) haengen dv:-Tripel an;
    Devices ohne die Property bleiben draussen."""
    from ConduitRemote.tests.stub_live import Device

    song, send, stream = make_stream()
    comp = Device("Compressor", "Compressor2")
    comp.gain_reduction = 0.3          # LOM-Property nur bei Dynamics-Devices
    eq = Device("EQ Eight", "Eq8")     # hat KEIN gain_reduction
    song.tracks[0].devices.append(comp)
    song.tracks[0].devices.append(eq)

    stream.on_subscribe()
    stream.on_tick(1)

    _address, args = send.sent[0]
    triplets = {args[i]: (args[i + 1], args[i + 2])
                for i in range(0, len(args), 3)}

    comp_id = stable_ids.get_id(comp, stable_ids.DEVICE_PREFIX)
    eq_id = stable_ids.get_id(eq, stable_ids.DEVICE_PREFIX)
    assert triplets[comp_id] == (0.3, 0.3)
    assert eq_id not in triplets


def test_gain_reduction_counts_against_silence_dedupe():
    from ConduitRemote.tests.stub_live import Device

    song, send, stream = make_stream()
    comp = Device("Compressor", "Compressor2")
    comp.gain_reduction = 0.0
    song.tracks[0].devices.append(comp)

    stream.on_subscribe()
    stream.on_tick(1)   # alles still -> ein Null-Frame
    stream.on_tick(2)
    assert len(send.sent) == 1

    comp.gain_reduction = 0.5   # nur GR bewegt sich -> Frame geht raus
    stream.on_tick(3)
    assert len(send.sent) == 2
