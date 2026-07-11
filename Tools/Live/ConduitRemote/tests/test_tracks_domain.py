import pytest

from ConduitRemote.sync import stable_ids
from ConduitRemote.sync.tracks import TracksDomain
from ConduitRemote.tests.stub_live import Song, FakeSender, Track


@pytest.fixture(autouse=True)
def _clear_registry():
    stable_ids.clear()
    yield
    stable_ids.clear()


def make_domain(num_tracks=2, num_sends=2):
    song = Song(num_tracks=num_tracks, num_scenes=4, num_sends=num_sends)
    sender = FakeSender()
    domain = TracksDomain(song, sender)
    return song, sender, domain


def test_snapshot_contains_tracks_returns_master():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    address, seq, payload = sender.last()
    assert address == "/remote/state/tracks/snapshot"

    # 2 regular + 2 return + 1 master + 3 Skalar-Keys (Block H v2:
    # selected / conduit_focus / input_options)
    assert len(payload) == 8
    assert payload["selected"] == ""
    assert payload["conduit_focus"] == ""
    assert payload["input_options"][0] == "All Ins"
    tr1 = payload["tr:1"]
    assert tr1 == {"name": "Track 1", "color": 0xFF0000, "kind": "audio", "index": 0}
    tr2 = payload["tr:2"]
    assert tr2["index"] == 1
    rt1 = payload["rt:1"]
    assert rt1["kind"] == "return"
    ma = payload["ma:1"]
    assert ma["kind"] == "master"
    assert ma["index"] == 0


def test_midi_track_kind():
    song = Song(num_tracks=0, num_scenes=4, num_sends=0)
    song.tracks = [Track("Drums", has_midi_input=True)]
    sender = FakeSender()
    domain = TracksDomain(song, sender)
    domain.on_subscribe()
    payload = sender.last()[2]
    assert payload["tr:1"]["kind"] == "midi"


def test_rename_diffs_only_that_track():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    sender.clear()

    song.tracks[0].name = "Kick"
    domain.on_tick(1)

    address, seq, diff = sender.last()
    assert address == "/remote/state/tracks/diff"
    assert diff == {"tr:1": {"name": "Kick", "color": 0xFF0000, "kind": "audio", "index": 0}}


def test_color_change_diffs():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    sender.clear()

    song.tracks[0].color = 0x00FF00
    domain.on_tick(1)

    diff = sender.last()[2]
    assert diff["tr:1"]["color"] == 0x00FF00


def test_no_change_no_tick_send():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    sender.clear()
    domain.on_tick(1)
    assert sender.sent == []


def test_removing_track_diffs_none_and_ids_stable_for_survivors():
    song, sender, domain = make_domain(num_tracks=3)
    domain.on_subscribe()
    first_payload = sender.last()[2]
    survivor_id = None
    for sid, info in first_payload.items():
        if sid.startswith("tr:") and info["name"] == "Track 3":
            survivor_id = sid
    assert survivor_id is not None
    sender.clear()

    removed = song.tracks.pop(0)
    song.notify("tracks")
    domain.on_tick(1)

    diff = sender.last()[2]
    # the removed track's id must be gone (None)
    removed_ids = [k for k, v in diff.items() if v is None]
    assert len(removed_ids) == 1
    # survivor keeps its id, just index shifts
    assert diff[survivor_id]["index"] == 1


def test_rebind_after_structure_change_new_track_listens():
    song, sender, domain = make_domain(num_tracks=1)
    domain.on_subscribe()
    sender.clear()

    new_track = Track("New")
    song.tracks.append(new_track)
    song.notify("tracks")
    domain.on_tick(1)   # structure change diff
    sender.clear()

    new_track.name = "Renamed"
    domain.on_tick(1)

    diff = sender.last()[2]
    assert list(diff.values())[0]["name"] == "Renamed"


def test_rebind_detaches_old_track_no_duplicate_listener():
    song, sender, domain = make_domain(num_tracks=1)
    domain.on_subscribe()
    removed_track = song.tracks[0]
    song.tracks = []
    song.notify("tracks")
    domain.on_tick(1)
    sender.clear()

    # changing the removed track's name must not mark_dirty (unbound)
    removed_track.name = "Ghost"
    domain.on_tick(1)
    assert sender.sent == []


def test_unsubscribed_emits_nothing():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    domain.on_unsubscribe()
    sender.clear()

    song.tracks[0].name = "X"
    domain.on_tick(1)

    assert sender.sent == []


def test_detach_removes_listeners():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    domain.detach()
    sender.clear()
    domain._dirty = False
    domain.subscribed = True

    song.tracks[0].name = "Y"
    domain.on_tick(1)

    assert sender.sent == []
