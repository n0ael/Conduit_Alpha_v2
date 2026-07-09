import pytest

from ConduitRemote.sync import stable_ids
from ConduitRemote.sync.mixer import MixerDomain
from ConduitRemote.tests.stub_live import Song, FakeSender


@pytest.fixture(autouse=True)
def _clear_registry():
    stable_ids.clear()
    yield
    stable_ids.clear()


def make_domain(num_tracks=2, num_sends=2):
    song = Song(num_tracks=num_tracks, num_scenes=4, num_sends=num_sends)
    sender = FakeSender()
    domain = MixerDomain(song, sender)
    return song, sender, domain


def test_snapshot_shape():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    payload = sender.last()[2]

    assert len(payload) == 5   # 2 tracks + 2 returns + master
    tr1 = payload["tr:1"]
    assert tr1["vol"] == pytest.approx(0.85)
    assert tr1["pan"] == pytest.approx(0.0)
    assert tr1["sends"] == [0.0, 0.0]
    assert tr1["mute"] is False
    assert tr1["solo"] is False
    assert tr1["arm"] is False

    master = payload["ma:1"]
    assert "vol" in master and "pan" in master
    assert "sends" not in master
    assert "mute" not in master
    assert "solo" not in master
    assert "arm" not in master

    # Returns (LOM: mute/solo ja, arm WIRFT — Feldtest 09.07.2026):
    # der Key fehlt komplett, statt die Domain zu killen
    rt1 = payload["rt:1"]
    assert rt1["mute"] is False
    assert rt1["solo"] is False
    assert "arm" not in rt1


def test_snapshot_survives_lom_capability_errors():
    """Der eigentliche Feldtest-Bug: on_subscribe/collect() liefen in
    track.arm auf Returns bzw. mute/solo-Listener auf dem Master und die
    Domain starb (5-Fehler-Budget). Mit Guards liefert sie einfach."""
    song, sender, domain = make_domain()

    domain.on_subscribe()      # bindet Listener inkl. Master — darf nicht werfen
    assert sender.last() is not None

    song.tracks[0].mixer_device.volume.value = 0.3
    domain.on_tick(1)          # collect() über Returns+Master — darf nicht werfen
    assert sender.last()[2]["tr:1"]["vol"] == pytest.approx(0.3)

    domain.detach()            # unbind nur, was gebunden wurde


def test_volume_change_diffs_only_that_track():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    sender.clear()

    song.tracks[0].mixer_device.volume.value = 0.5
    domain.on_tick(1)

    address, seq, diff = sender.last()
    assert address == "/remote/state/mixer/diff"
    assert list(diff.keys()) == ["tr:1"]
    assert diff["tr:1"]["vol"] == pytest.approx(0.5)


def test_pan_change_diffs():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    sender.clear()

    song.tracks[0].mixer_device.panning.value = -0.5
    domain.on_tick(1)

    diff = sender.last()[2]
    assert diff["tr:1"]["pan"] == pytest.approx(-0.5)


def test_send_change_diffs():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    sender.clear()

    song.tracks[0].mixer_device.sends[1].value = 0.75
    domain.on_tick(1)

    diff = sender.last()[2]
    assert diff["tr:1"]["sends"] == [0.0, 0.75]


def test_mute_solo_arm_diffs():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    sender.clear()

    song.tracks[0].mute = True
    domain.on_tick(1)
    assert sender.last()[2]["tr:1"]["mute"] is True

    sender.clear()
    song.tracks[0].solo = True
    domain.on_tick(1)
    assert sender.last()[2]["tr:1"]["solo"] is True

    sender.clear()
    song.tracks[0].arm = True
    domain.on_tick(1)
    assert sender.last()[2]["tr:1"]["arm"] is True


def test_master_volume_change_diffs():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    sender.clear()

    song.master_track.mixer_device.volume.value = 0.6
    domain.on_tick(1)

    diff = sender.last()[2]
    assert diff["ma:1"]["vol"] == pytest.approx(0.6)
    assert "sends" not in diff["ma:1"]


def test_removing_track_diffs_none():
    song, sender, domain = make_domain(num_tracks=2)
    domain.on_subscribe()
    sender.clear()

    song.tracks.pop(0)
    song.notify("tracks")
    domain.on_tick(1)

    diff = sender.last()[2]
    assert diff["tr:1"] is None


def test_shares_stable_ids_with_tracks_domain():
    from ConduitRemote.sync.tracks import TracksDomain

    song = Song(num_tracks=2, num_scenes=4, num_sends=2)
    sender_a = FakeSender()
    sender_b = FakeSender()
    tracks_domain = TracksDomain(song, sender_a)
    mixer_domain = MixerDomain(song, sender_b)

    tracks_domain.on_subscribe()
    mixer_domain.on_subscribe()

    tracks_payload = sender_a.last()[2]
    mixer_payload = sender_b.last()[2]
    assert set(tracks_payload.keys()) == set(mixer_payload.keys())


def test_unsubscribed_emits_nothing():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    domain.on_unsubscribe()
    sender.clear()

    song.tracks[0].mixer_device.volume.value = 0.1
    domain.on_tick(1)

    assert sender.sent == []


def test_detach_removes_listeners():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    domain.detach()
    sender.clear()
    domain._dirty = False
    domain.subscribed = True

    song.tracks[0].mixer_device.volume.value = 0.1
    domain.on_tick(1)

    assert sender.sent == []
