"""/live/song/* command handlers: transport + global song settings."""

import logging

from . import _util

logger = logging.getLogger(__name__)

_TEMPO_MIN = 20.0
_TEMPO_MAX = 999.0


def _start_playing(ctx, args):
    song = ctx.song
    if song is None:
        return
    song.start_playing()


def _stop_playing(ctx, args):
    song = ctx.song
    if song is None:
        return
    song.stop_playing()


def _continue_playing(ctx, args):
    song = ctx.song
    if song is None:
        return
    fn = getattr(song, "continue_playing", None)
    if callable(fn):
        fn()
    else:
        logger.debug("continue_playing: not supported by this song object")


def _set_tempo(ctx, args):
    if len(args) < 1:
        return
    song = ctx.song
    if song is None:
        return
    song.tempo = _util.clamp(
        _util.as_float(args[0], song.tempo), _TEMPO_MIN, _TEMPO_MAX)


def _set_metronome(ctx, args):
    if len(args) < 1:
        return
    song = ctx.song
    if song is None:
        return
    song.metronome = _util.as_bool(args[0])


def _set_session_record(ctx, args):
    if len(args) < 1:
        return
    song = ctx.song
    if song is None:
        return
    song.session_record = _util.as_bool(args[0])


def _undo(ctx, args):
    song = ctx.song
    if song is None:
        return
    fn = getattr(song, "undo", None)
    if callable(fn):
        fn()


def _redo(ctx, args):
    song = ctx.song
    if song is None:
        return
    fn = getattr(song, "redo", None)
    if callable(fn):
        fn()


def _set_selected_track(ctx, args):
    """View-Selektion (M4: Browser-Load-Ziel) — track_ref wie ueberall
    (Index oder Stable-ID); LOM-sicher via try/except."""
    if len(args) < 1:
        return
    track = ctx.resolve_track(args[0])
    if track is None:
        logger.debug("selected_track: unknown track %r", args[0])
        return
    try:
        ctx.song.view.selected_track = track
    except Exception:
        logger.exception("selected_track failed")


def register_all(registry):
    registry.register("/live/song/start_playing", _start_playing)
    registry.register("/live/song/stop_playing", _stop_playing)
    registry.register("/live/song/continue_playing", _continue_playing)
    registry.register("/live/song/set/tempo", _set_tempo)
    registry.register("/live/song/set/metronome", _set_metronome)
    registry.register("/live/song/set/session_record", _set_session_record)
    registry.register("/live/song/undo", _undo)
    registry.register("/live/song/redo", _redo)
    registry.register("/live/song/set/selected_track", _set_selected_track)
