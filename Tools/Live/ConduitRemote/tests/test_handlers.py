import pytest

from ConduitRemote.tests.stub_live import Song
from ConduitRemote.handlers.registry import CommandContext, CommandRegistry
from ConduitRemote.handlers import song as song_handlers
from ConduitRemote.handlers import track as track_handlers
from ConduitRemote.handlers import session as session_handlers
from ConduitRemote.sync.inputfocus import InputFocusService


@pytest.fixture
def rig():
    song = Song(num_tracks=3, num_scenes=4, num_sends=2)
    registry = CommandRegistry()
    song_handlers.register_all(registry)
    track_handlers.register_all(registry)
    session_handlers.register_all(registry)
    ctx = CommandContext(lambda: song,
                         input_focus=InputFocusService(song))
    return song, registry, ctx


def dispatch(rig, address, args):
    _song, registry, ctx = rig
    return registry.dispatch(address, args, ctx)


# -- song / transport --------------------------------------------------------

def test_start_stop_playing(rig):
    song, _registry, _ctx = rig
    dispatch(rig, "/live/song/start_playing", [])
    assert song.is_playing is True
    dispatch(rig, "/live/song/stop_playing", [])
    assert song.is_playing is False


def test_continue_playing_missing_on_stub_does_not_raise(rig):
    # stub_live.Song has no continue_playing(); handler must no-op safely.
    assert dispatch(rig, "/live/song/continue_playing", []) is True


def test_set_tempo_clamped(rig):
    song, _r, _c = rig
    dispatch(rig, "/live/song/set/tempo", [140.0])
    assert song.tempo == pytest.approx(140.0)
    dispatch(rig, "/live/song/set/tempo", [5000.0])
    assert song.tempo == pytest.approx(999.0)
    dispatch(rig, "/live/song/set/tempo", [-10.0])
    assert song.tempo == pytest.approx(20.0)


def test_set_metronome_and_session_record(rig):
    song, _r, _c = rig
    dispatch(rig, "/live/song/set/metronome", [1])
    assert song.metronome is True
    dispatch(rig, "/live/song/set/metronome", [0])
    assert song.metronome is False
    dispatch(rig, "/live/song/set/session_record", [1])
    assert song.session_record is True


def test_undo_redo(rig):
    song, _r, _c = rig
    dispatch(rig, "/live/song/undo", [])
    assert song.__dict__.get("did_undo") is True
    dispatch(rig, "/live/song/redo", [])
    assert song.__dict__.get("did_redo") is True


# -- track --------------------------------------------------------------------

def test_track_volume_and_panning(rig):
    song, _r, _c = rig
    dispatch(rig, "/live/track/set/volume", [1, 0.5])
    assert song.tracks[1].mixer_device.volume.value == pytest.approx(0.5)
    dispatch(rig, "/live/track/set/panning", [1, -0.25])
    assert song.tracks[1].mixer_device.panning.value == pytest.approx(-0.25)


def test_track_volume_clamped(rig):
    song, _r, _c = rig
    dispatch(rig, "/live/track/set/volume", [0, 5.0])
    assert song.tracks[0].mixer_device.volume.value == pytest.approx(1.0)
    dispatch(rig, "/live/track/set/volume", [0, -5.0])
    assert song.tracks[0].mixer_device.volume.value == pytest.approx(0.0)


def test_track_send(rig):
    song, _r, _c = rig
    dispatch(rig, "/live/track/set/send", [0, 1, 0.75])
    assert song.tracks[0].mixer_device.sends[1].value == pytest.approx(0.75)


def test_track_send_out_of_range_ignored(rig):
    song, _r, _c = rig
    before = song.tracks[0].mixer_device.sends[0].value
    dispatch(rig, "/live/track/set/send", [0, 99, 0.75])
    assert song.tracks[0].mixer_device.sends[0].value == before


def test_track_mute_solo_arm(rig):
    song, _r, _c = rig
    dispatch(rig, "/live/track/set/mute", [0, 1])
    assert song.tracks[0].mute is True
    dispatch(rig, "/live/track/set/solo", [0, 1])
    assert song.tracks[0].solo is True
    dispatch(rig, "/live/track/set/arm", [0, 1])
    assert song.tracks[0].arm is True
    dispatch(rig, "/live/track/set/arm", [0, 0])
    assert song.tracks[0].arm is False


def test_track_arm_respects_can_be_armed(rig):
    song, _r, _c = rig
    song.tracks[0].can_be_armed = False
    dispatch(rig, "/live/track/set/arm", [0, 1])
    assert song.tracks[0].arm is False


def test_track_ref_out_of_range_ignored(rig):
    # must not raise
    assert dispatch(rig, "/live/track/set/volume", [99, 0.5]) is True


def test_stop_all_clips(rig):
    song, _r, _c = rig
    dispatch(rig, "/live/track/stop_all_clips", [0])
    assert song.tracks[0].__dict__.get("stopped_all") is True


# -- return / master ------------------------------------------------------------

def test_return_volume_panning(rig):
    song, _r, _c = rig
    dispatch(rig, "/live/return/set/volume", [0, 0.6])
    assert song.return_tracks[0].mixer_device.volume.value == pytest.approx(0.6)
    dispatch(rig, "/live/return/set/panning", [1, 0.3])
    assert song.return_tracks[1].mixer_device.panning.value == pytest.approx(0.3)


def test_return_out_of_range_ignored(rig):
    assert dispatch(rig, "/live/return/set/volume", [99, 0.6]) is True


def test_master_volume_panning(rig):
    song, _r, _c = rig
    dispatch(rig, "/live/master/set/volume", [0.42])
    assert song.master_track.mixer_device.volume.value == pytest.approx(0.42)
    dispatch(rig, "/live/master/set/panning", [-0.5])
    assert song.master_track.mixer_device.panning.value == pytest.approx(-0.5)


# -- clip_slot / scene -----------------------------------------------------------

def test_clip_slot_fire_and_stop(rig):
    song, _r, _c = rig
    dispatch(rig, "/live/clip_slot/fire", [0, 2])
    assert song.tracks[0].clip_slots[2].__dict__.get("fired") is True
    dispatch(rig, "/live/clip_slot/stop", [0, 2])  # must not raise


def test_clip_slot_out_of_range_ignored(rig):
    assert dispatch(rig, "/live/clip_slot/fire", [0, 999]) is True
    assert dispatch(rig, "/live/clip_slot/fire", [999, 0]) is True


def test_scene_fire(rig):
    song, _r, _c = rig
    dispatch(rig, "/live/scene/fire", [1])
    assert song.scenes[1].__dict__.get("fired") is True


def test_scene_fire_out_of_range_ignored(rig):
    assert dispatch(rig, "/live/scene/fire", [999]) is True


def test_unknown_address_dispatch_returns_false(rig):
    assert dispatch(rig, "/live/does/not/exist", []) is False


# -- midi_input_focus rev5 (Block H: Conduit Grid-Track-Selector) --------------
# Vollstaendige Routing-Semantik: tests/test_inputfocus.py -- hier nur der
# Command-Weg (Handler -> Service).

def _make_midi(track):
    track.__dict__["has_midi_input"] = True


def test_midi_input_focus_routes_target_and_all_ins_to_master(rig):
    song, _r, _c = rig
    _make_midi(song.tracks[0])
    _make_midi(song.tracks[2])   # tracks[1] bleibt Audio

    dispatch(rig, "/live/song/set/midi_input_focus",
             [2, "Conduit Grid MPE", "FromPush"])

    target = song.tracks[2]
    other = song.tracks[0]
    assert target.current_monitoring_state == 0                      # In
    assert target.input_routing_type.display_name == "Conduit Grid MPE"
    # anderer All-Ins-MIDI-Track: Input -> Master, Monitor unangetastet
    assert other.input_routing_type.display_name == "FromPush"
    assert other.current_monitoring_state == 1

    # Audio-Track komplett unberuehrt
    assert song.tracks[1].current_monitoring_state == 1
    assert song.tracks[1].input_routing_type.display_name == "All Ins"


def test_midi_input_focus_ignores_legacy_follow_argument(rig):
    song, _r, _c = rig
    _make_midi(song.tracks[0])
    assert dispatch(rig, "/live/song/set/midi_input_focus",
                    [0, "Conduit Grid MPE", "FromPush", 1]) is True
    assert song.tracks[0].current_monitoring_state == 0


def test_midi_input_focus_unknown_track_ignored(rig):
    assert dispatch(rig, "/live/song/set/midi_input_focus",
                    [999, "Conduit Grid MPE", ""]) is True


def test_midi_input_focus_without_service_is_noop(rig):
    song, registry, _c = rig
    bare = CommandContext(lambda: song)
    assert registry.dispatch("/live/song/set/midi_input_focus",
                             [0, "Conduit Grid MPE", ""], bare) is True
