"""InputFocusService (Block H v2): Fokus-Routing + Follow-Selection.

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


# -- set_focus -----------------------------------------------------------------

def test_focus_routes_target_others_off_selected_to_master():
    song = make_song()
    song.view.selected_track = song.tracks[1]
    service = InputFocusService(song)

    service.set_focus(song.tracks[0], GRID, MASTER, True)

    assert monitor(song.tracks[0]) == 0 and routing(song.tracks[0]) == GRID
    assert monitor(song.tracks[1]) == 1 and routing(song.tracks[1]) == MASTER
    assert monitor(song.tracks[2]) == 2 and routing(song.tracks[2]) == "All Ins"
    assert service.focus_stable_id() != ""


def test_focus_leaves_explicitly_routed_tracks_alone():
    song = make_song()
    explicit = song.tracks[1]
    explicit.input_routing_type = explicit.available_input_routing_types[3]  # K1

    service = InputFocusService(song)
    service.set_focus(song.tracks[0], GRID, MASTER, True)

    # kein All Ins -> weder Monitor noch Routing angefasst
    assert monitor(explicit) == 1
    assert routing(explicit) == "K1 (Port 1)"


def test_focus_switch_restores_previous_focus():
    song = make_song()
    service = InputFocusService(song)

    service.set_focus(song.tracks[0], GRID, MASTER, True)
    service.set_focus(song.tracks[1], GRID, MASTER, True)

    # alter Fokus: zurueck auf All Ins + Off (sein Input war noch der Grid-Port)
    assert monitor(song.tracks[0]) == 2
    assert routing(song.tracks[0]) == "All Ins"
    assert monitor(song.tracks[1]) == 0 and routing(song.tracks[1]) == GRID


def test_focus_switch_never_overwrites_user_rerouting():
    song = make_song()
    service = InputFocusService(song)
    service.set_focus(song.tracks[0], GRID, MASTER, True)

    # User hat den Fokus-Track inzwischen selbst umgeroutet
    old = song.tracks[0]
    old.input_routing_type = old.available_input_routing_types[3]  # K1

    service.set_focus(song.tracks[1], GRID, MASTER, True)
    assert routing(old) == "K1 (Port 1)"   # bleibt unangetastet


# -- follow selection -----------------------------------------------------------

def test_follow_moves_new_selection_and_restores_previous():
    song = make_song()
    song.view.selected_track = song.tracks[1]
    service = InputFocusService(song)
    service.set_focus(song.tracks[0], GRID, MASTER, True)
    assert routing(song.tracks[1]) == MASTER

    # Push selektiert Track 2 -> Listener feuert (Stub-__setattr__)
    song.view.selected_track = song.tracks[2]

    assert monitor(song.tracks[1]) == 2                    # restore: Off
    assert routing(song.tracks[1]) == "All Ins"            # restore: All Ins
    assert monitor(song.tracks[2]) == 1                    # neu: Auto
    assert routing(song.tracks[2]) == MASTER


def test_follow_never_touches_focus_track():
    song = make_song()
    service = InputFocusService(song)
    service.set_focus(song.tracks[0], GRID, MASTER, True)

    song.view.selected_track = song.tracks[0]   # Fokus-Track selektiert

    assert monitor(song.tracks[0]) == 0
    assert routing(song.tracks[0]) == GRID


def test_follow_disabled_does_nothing():
    song = make_song()
    service = InputFocusService(song)
    service.set_focus(song.tracks[0], GRID, MASTER, False)

    song.view.selected_track = song.tracks[2]

    assert monitor(song.tracks[2]) == 2          # blieb Off (aus set_focus)
    assert routing(song.tracks[2]) == "All Ins"


def test_follow_without_focus_does_nothing():
    song = make_song()
    service = InputFocusService(song)
    service.set_follow(True)

    song.view.selected_track = song.tracks[1]   # kein Listener gebunden

    assert monitor(song.tracks[1]) == 1
    assert routing(song.tracks[1]) == "All Ins"


def test_set_follow_toggles_live():
    song = make_song()
    service = InputFocusService(song)
    service.set_focus(song.tracks[0], GRID, MASTER, True)

    service.set_follow(False)
    song.view.selected_track = song.tracks[1]
    assert routing(song.tracks[1]) == "All Ins"   # Follow aus

    service.set_follow(True)
    song.view.selected_track = song.tracks[2]
    assert routing(song.tracks[2]) == MASTER      # Follow wieder an


# -- robustness -------------------------------------------------------------------

def test_service_survives_returns_and_master():
    song = make_song()
    service = InputFocusService(song)
    # Returns/Master werfen auf Routing-Attribute -- set_focus iteriert nur
    # song.tracks; focus_stable_id/notify duerfen ebenfalls nie werfen.
    service.set_focus(song.tracks[0], GRID, MASTER, True)
    song.view.selected_track = song.return_tracks[0]
    assert service.focus_stable_id() != ""


def test_state_change_callback_fires():
    song = make_song()
    service = InputFocusService(song)
    calls = []
    service.on_state_changed = lambda: calls.append(1)

    service.set_focus(song.tracks[0], GRID, MASTER, True)
    assert calls

    calls.clear()
    song.view.selected_track = song.tracks[1]
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
    service.set_focus(song.tracks[0], GRID, MASTER, True)

    state = domain.collect()
    assert state["conduit_focus"] == service.focus_stable_id()
    assert state["selected"] == stable_ids.get_id(
        song.tracks[1], stable_ids.TRACK_PREFIX)
    assert "Conduit Grid MPE" in state["input_options"]
    assert state["input_options"][0] == "All Ins"


# -- Feldtest-Fixes 11.07.2026 ---------------------------------------------------

def test_focus_switch_restores_stale_moved_track():
    # Fokus F, Push-Selektion S wurde auf Master bewegt; Live selektiert
    # danach etwas anderes, OHNE dass der Follow-Listener lief (Ausfall-
    # Simulation wie in Live) -- ein neuer set_focus darf S nicht auf dem
    # Master-Input haengen lassen.
    song = make_song()   # tracks[0..2] midi, tracks[3] audio
    song.view.selected_track = song.tracks[1]
    service = InputFocusService(song)
    service.set_focus(song.tracks[0], GRID, MASTER, True)
    assert routing(song.tracks[1]) == MASTER

    service.detach()
    song.view.selected_track = song.tracks[3]   # Audio-Track, Listener tot

    service.set_focus(song.tracks[2], GRID, MASTER, True)

    assert monitor(song.tracks[1]) == 2
    assert routing(song.tracks[1]) == "All Ins"   # nicht mehr auf Master


def test_poll_follows_selection_without_listener():
    # Feldtest 11.07.2026: der selected_track-Listener kann in Live still
    # ausfallen -- poll() (Manager-Tick) muss den Wechsel trotzdem fahren.
    song = make_song()
    song.view.selected_track = song.tracks[1]
    service = InputFocusService(song)
    service.set_focus(song.tracks[0], GRID, MASTER, True)

    service.detach()   # Listener-Ausfall simulieren
    song.view.selected_track = song.tracks[2]
    assert routing(song.tracks[2]) == "All Ins"   # noch nichts passiert

    service.poll()

    assert monitor(song.tracks[1]) == 2
    assert routing(song.tracks[1]) == "All Ins"
    assert monitor(song.tracks[2]) == 1
    assert routing(song.tracks[2]) == MASTER

    # Dedupe: erneuter poll ohne Wechsel tut nichts (kein Flattern)
    calls = []
    service.on_state_changed = lambda: calls.append(1)
    service.poll()
    assert not calls


def test_listener_and_poll_do_not_double_fire():
    song = make_song()
    service = InputFocusService(song)
    service.set_focus(song.tracks[0], GRID, MASTER, True)

    song.view.selected_track = song.tracks[1]   # Listener feuert sofort
    assert routing(song.tracks[1]) == MASTER

    calls = []
    service.on_state_changed = lambda: calls.append(1)
    service.poll()   # gleicher Zustand -> Dedupe greift
    assert not calls
