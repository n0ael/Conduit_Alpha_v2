"""InputFocusService (Block H v2 rev5): statisches Fokus-Routing.

User-Entscheidung 11.07.2026 abends: KEIN Selektions-Following mehr --
alle All-Ins-MIDI-Tracks wandern einmalig auf das Master-Device, Lives
eigene Arm-/Selektions-Mechanik uebernimmt; Monitoring bleibt Sache des
Users (nur ein stale OFF der frueheren Implementierung wird geheilt).

Stub-Realismus: nur regulaere Tracks tragen Monitoring/Routing
(Returns/Master werfen), Routing-Optionen sind
["All Ins", "Conduit Grid MPE", "FromPush", "K1 (Port 1)"].
"""

import pytest

from ConduitRemote.tests.stub_live import Song
from ConduitRemote.sync.inputfocus import InputFocusService
from ConduitRemote.sync import stable_ids

GRID = "Conduit Grid MPE"
MASTER = "FromPush"


@pytest.fixture(autouse=True)
def _clean_ids():
    stable_ids.clear()
    yield
    stable_ids.clear()


def make_song(num_midi=3):
    song = Song(num_tracks=num_midi + 1)   # letzter bleibt Audio
    for track in song.tracks[:num_midi]:
        track.__dict__["has_midi_input"] = True
    return song


def routing(track):
    return track.input_routing_type.display_name


def monitor(track):
    return track.current_monitoring_state


# -- set_focus (rev5: statische Aufteilung) --------------------------------------

def test_focus_routes_target_and_moves_all_ins_to_master():
    song = make_song()
    service = InputFocusService(song)

    service.set_focus(song.tracks[0], GRID, MASTER)

    # Ziel: Grid-Port + Monitor In
    assert monitor(song.tracks[0]) == 0 and routing(song.tracks[0]) == GRID
    # andere All-Ins-Tracks: Input -> Master, Monitor UNANGETASTET (Auto)
    assert routing(song.tracks[1]) == MASTER
    assert monitor(song.tracks[1]) == 1
    assert routing(song.tracks[2]) == MASTER
    assert monitor(song.tracks[2]) == 1
    # Audio-Track unberuehrt
    assert routing(song.tracks[3]) == "All Ins"
    assert service.focus_stable_id() != ""


def test_focus_leaves_foreign_inputs_alone():
    # User-Regel: Sequencer-/Hardware-Inputs sind tabu -- Input UND Monitor.
    song = make_song()
    seq = song.tracks[1]
    seq.input_routing_type = seq.available_input_routing_types[3]  # K1
    seq.current_monitoring_state = 0   # User hoert den Sequencer (In)

    service = InputFocusService(song)
    service.set_focus(song.tracks[0], GRID, MASTER)

    assert routing(seq) == "K1 (Port 1)"
    assert monitor(seq) == 0


def test_focus_does_not_touch_monitor_of_master_tracks():
    # Bereits auf Master geroutete Tracks (statischer Zustand) bleiben
    # komplett unangetastet -- auch ihr Monitoring.
    song = make_song()
    already = song.tracks[1]
    already.input_routing_type = already.available_input_routing_types[2]  # FromPush
    already.current_monitoring_state = 2   # User-gewolltes Off

    service = InputFocusService(song)
    service.set_focus(song.tracks[0], GRID, MASTER)

    assert routing(already) == MASTER
    assert monitor(already) == 2


def test_focus_heals_stale_off_when_moving_from_all_ins():
    # Ein All-Ins-Track mit Monitor Off (Altlast der Follow-Implementierung)
    # wird beim Umstellen einmalig auf Auto geheilt.
    song = make_song()
    stale = song.tracks[1]
    stale.current_monitoring_state = 2

    service = InputFocusService(song)
    service.set_focus(song.tracks[0], GRID, MASTER)

    assert routing(stale) == MASTER
    assert monitor(stale) == 1


def test_focus_switch_gives_previous_focus_back_to_master():
    song = make_song()
    service = InputFocusService(song)

    service.set_focus(song.tracks[0], GRID, MASTER)
    service.set_focus(song.tracks[1], GRID, MASTER)

    # alter Fokus: Master-Input + Auto (wieder normal mit Push spielbar)
    assert routing(song.tracks[0]) == MASTER
    assert monitor(song.tracks[0]) == 1
    assert monitor(song.tracks[1]) == 0 and routing(song.tracks[1]) == GRID


def test_focus_switch_never_overwrites_user_rerouting():
    song = make_song()
    service = InputFocusService(song)
    service.set_focus(song.tracks[0], GRID, MASTER)

    # User hat den Fokus-Track inzwischen selbst umgeroutet
    old = song.tracks[0]
    old.input_routing_type = old.available_input_routing_types[3]  # K1

    service.set_focus(song.tracks[1], GRID, MASTER)
    assert routing(old) == "K1 (Port 1)"   # bleibt unangetastet


def test_empty_master_leaves_all_ins_tracks_alone():
    # Ohne gewaehltes Master-Device (leerer Name) bleiben die
    # All-Ins-Tracks stehen -- nur das Ziel wird geroutet.
    song = make_song()
    service = InputFocusService(song)

    service.set_focus(song.tracks[0], GRID, "")

    assert routing(song.tracks[0]) == GRID
    assert routing(song.tracks[1]) == "All Ins"
    assert monitor(song.tracks[1]) == 1


def test_routing_pass_is_idempotent():
    song = make_song()
    service = InputFocusService(song)
    service.set_focus(song.tracks[0], GRID, MASTER)

    before = [(routing(t), monitor(t)) for t in song.tracks[:3]]
    service._apply_routing()
    after = [(routing(t), monitor(t)) for t in song.tracks[:3]]
    assert before == after


# -- robustness -------------------------------------------------------------------

def test_service_survives_returns_and_master():
    # Returns/Master werfen auf Routing-Attribute -- der Pass iteriert nur
    # song.tracks; focus_stable_id/notify duerfen ebenfalls nie werfen.
    song = make_song()
    service = InputFocusService(song)
    service.set_focus(song.tracks[0], GRID, MASTER)
    assert service.focus_stable_id() != ""


def test_state_change_callback_fires():
    song = make_song()
    service = InputFocusService(song)
    calls = []
    service.on_state_changed = lambda: calls.append(1)

    service.set_focus(song.tracks[0], GRID, MASTER)
    assert calls


def test_tracks_domain_exposes_focus_selected_and_options():
    from ConduitRemote.sync.tracks import TracksDomain

    class _Sender(object):
        def send_json(self, *args, **kwargs):
            pass

    song = make_song()
    service = InputFocusService(song)
    domain = TracksDomain(song, _Sender())
    domain.input_focus = service

    song.view.selected_track = song.tracks[1]
    service.set_focus(song.tracks[0], GRID, MASTER)

    state = domain.collect()
    assert state["conduit_focus"] == service.focus_stable_id()
    assert state["selected"] == stable_ids.get_id(
        song.tracks[1], stable_ids.TRACK_PREFIX)
    assert "Conduit Grid MPE" in state["input_options"]
    assert state["input_options"][0] == "All Ins"
