from ConduitRemote.sync.transport import TransportDomain
from ConduitRemote.tests.stub_live import Song, FakeSender


def make_domain():
    song = Song(num_tracks=2, num_scenes=4)
    sender = FakeSender()
    domain = TransportDomain(song, sender)
    return song, sender, domain


def test_subscribe_sends_snapshot():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    assert len(sender.sent) == 1
    address, seq, payload = sender.last()
    assert address == "/remote/state/transport/snapshot"
    assert seq == 1
    assert payload == {
        "is_playing": False,
        "tempo": 120.0,
        "metronome": False,
        "session_record": False,
        "sig_num": 4,
        "sig_den": 4,
        "bar": 1,
        "beat": 1,
    }


def test_tempo_change_marks_dirty_and_diffs_only_that_key():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    sender.clear()

    song.tempo = 128.0
    domain.on_tick(1)

    assert len(sender.sent) == 1
    address, seq, diff = sender.last()
    assert address == "/remote/state/transport/diff"
    assert diff == {"tempo": 128.0}


def test_is_playing_change_diffs():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    sender.clear()

    song.start_playing()
    domain.on_tick(1)

    assert sender.last()[2] == {"is_playing": True}


def test_no_change_no_send_on_tick():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    sender.clear()

    domain.on_tick(1)

    assert sender.sent == []


def test_unsubscribed_domain_emits_nothing_on_tick():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    domain.on_unsubscribe()
    sender.clear()

    song.tempo = 140.0
    domain.on_tick(1)

    assert sender.sent == []


def test_detach_removes_listeners_change_after_detach_not_dirty():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    domain.detach()
    sender.clear()
    domain._dirty = False

    song.tempo = 140.0   # listener removed, should not mark dirty

    assert domain._dirty is False
    domain.subscribed = True
    domain.on_tick(1)
    assert sender.sent == []


def test_double_attach_does_not_duplicate_listeners():
    song, sender, domain = make_domain()
    domain.attach()
    domain.attach()   # should be a no-op guard
    sender.clear()
    domain.subscribed = True
    domain._dirty = False

    song.tempo = 150.0
    domain.on_tick(1)

    # exactly one diff message, not two (which would happen if the listener
    # had been registered twice and fired mark_dirty/dirty-triggered sends
    # in a way that duplicated output)
    assert len(sender.sent) == 1


# -- Song-Position (Live-Remote-Bridge, 07/2026) -------------------------------

def test_beat_change_diffs_bar_and_beat():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    sender.clear()

    song.current_song_time = 5.0   # 4/4: Takt 2, Beat 2
    domain.on_tick(1)

    assert sender.last()[2] == {"bar": 2, "beat": 2}


def test_sub_beat_movement_sends_no_diff():
    song, sender, domain = make_domain()
    domain.on_subscribe()
    sender.clear()

    # Listener feuert (dirty), aber die quantisierten Keys aendern sich
    # nicht -> compute_diff findet nichts, kein Send (Diff-Drossel).
    song.current_song_time = 0.25
    domain.on_tick(1)
    song.current_song_time = 0.75
    domain.on_tick(2)

    assert sender.sent == []


def test_signature_affects_bar_length():
    song, sender, domain = make_domain()
    song.__dict__["signature_numerator"] = 3   # 3/4: Takt = 3 Beats
    domain.on_subscribe()
    sender.clear()

    song.current_song_time = 3.0   # Takt 2, Beat 1 (beat unveraendert -> nur bar im Diff)
    domain.on_tick(1)

    assert sender.last()[2] == {"bar": 2}


def test_missing_beats_song_time_capability_omits_keys():
    song, sender, domain = make_domain()

    def _raise():
        raise RuntimeError("not supported")

    song.__dict__["get_current_beats_song_time"] = _raise
    domain.on_subscribe()

    payload = sender.last()[2]
    assert "bar" not in payload
    assert "beat" not in payload
    assert payload["tempo"] == 120.0   # Domain lebt weiter
