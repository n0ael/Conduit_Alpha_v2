"""Command dispatch: registry + execution context.

Handlers are plain functions ``fn(ctx, args)`` registered under an exact
OSC address.  CommandContext gives handlers access to the live song and a
track resolver that accepts either a raw index (int, or numeric string as
a fallback) into song.tracks, or a stable-id string delegated to a
resolver callable injected by the sync layer once it is wired up.

CommandRegistry.dispatch() is called both from the main-thread queue drain
and, for whitelisted fast addresses, directly from the OSC receiver
thread - it must stay thread-tolerant: no shared mutable state beyond the
handlers dict, which is populated once at startup and only read afterwards.
"""

import logging

logger = logging.getLogger(__name__)


class CommandContext(object):
    def __init__(self, get_song, track_resolver=None, device_resolver=None):
        self._get_song = get_song
        self._track_resolver = track_resolver
        self._device_resolver = device_resolver

    @property
    def song(self):
        return self._get_song()

    def resolve_device(self, ref):
        """Stable-ID ("dv:3") -> Device-Objekt, None wenn unbekannt."""
        if self._device_resolver is None or not isinstance(ref, str):
            return None
        try:
            return self._device_resolver(ref)
        except Exception:
            logger.exception("device resolver failed for %r", ref)
            return None

    def resolve_track(self, ref):
        song = self.song
        if song is None:
            return None
        tracks = song.tracks
        if isinstance(ref, bool):
            return None
        if isinstance(ref, int):
            if 0 <= ref < len(tracks):
                return tracks[ref]
            return None
        if isinstance(ref, str):
            if self._track_resolver is not None:
                try:
                    return self._track_resolver(ref)
                except Exception:
                    logger.exception("track resolver failed for %r", ref)
                    return None
            # Sync layer not wired yet: numeric-string fallback only.
            try:
                index = int(ref)
            except (TypeError, ValueError):
                return None
            if 0 <= index < len(tracks):
                return tracks[index]
            return None
        return None


class CommandRegistry(object):
    def __init__(self):
        self._handlers = {}

    def register(self, address, fn):
        self._handlers[address] = fn

    def addresses(self):
        return list(self._handlers.keys())

    def dispatch(self, address, args, ctx):
        handler = self._handlers.get(address)
        if handler is None:
            return False
        try:
            handler(ctx, args)
        except Exception:
            logger.exception("command handler failed for %s", address)
        return True
