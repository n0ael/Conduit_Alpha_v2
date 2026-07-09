"""Stable string IDs for Live objects (tracks/returns/master/scenes).

Live's LOM objects (Track, Scene, ...) do not carry a stable identifier of
their own that survives across a script session in a form convenient to
hand to an OSC client.  Track/scene *index* changes whenever the user
reorders things in Live, which would break any client that cached indices.
This module hands out small monotonic "prefix:N" strings keyed by python
object identity (`id(obj)`) so a client-visible ID never changes for the
lifetime of the Live set, no matter how much reordering happens.

API:
    get_id(obj, prefix) -> str        e.g. "tr:1", "sc:4"
    find(objects, stable_id, prefix) -> index or None
    clear()                            reset all state (tests / new Live set)

Shared prefix constants (both TracksDomain and MixerDomain must agree on
these so a track's mixer state and structure state use the SAME id):
    TRACK_PREFIX  = "tr"   regular (non-return, non-master) tracks
    RETURN_PREFIX = "rt"   return tracks
    MASTER_PREFIX = "ma"   the single master track
    SCENE_PREFIX  = "sc"   scenes

KNOWN LIMITATION: `id(obj)` can, in general CPython, be reused after the
object is garbage collected.  We defend against that by keeping a strong
reference to every object we've ever assigned an id to (see `_keepalive`
below), so as long as this module is alive the objects backing our ids are
too, and `id()` cannot be recycled out from under us.  In production this
is a non-issue anyway: Live keeps every Track/Scene object referenced by
`song.tracks`/`song.scenes` alive for the entire lifetime of the loaded Live
set, and `clear()` is only meant to be called when a new Live set is loaded
(i.e. when every previous id is meant to become invalid anyway).
"""

TRACK_PREFIX = "tr"
RETURN_PREFIX = "rt"
MASTER_PREFIX = "ma"
SCENE_PREFIX = "sc"
DEVICE_PREFIX = "dv"

_counters = {}   # prefix -> next integer to hand out
_ids = {}        # identity key -> stable_id string
_keepalive = {}  # identity key -> obj (prevents id() reuse, see module docstring)


def _identity(obj):
    """Session-stable identity key for a LOM object.

    FELDTEST-BEFUND 09.07.2026: Lives Boost.Python-Wrapper sind NICHT
    identitaetsstabil - jeder Zugriff auf song.tracks kann neue
    Wrapper-Objekte liefern, id(obj) zerfaellt damit im echten Live
    (Stable-IDs wurden pro Tick neu vergeben, der track_ref-Resolver fand
    nichts mehr). `_live_ptr` ist die kanonische, sessionstabile Identitaet
    der LOM-Objekte; id(obj) bleibt nur als Fallback fuer die Test-Stubs.
    """
    ptr = getattr(obj, "_live_ptr", None)
    if ptr is not None:
        return ("ptr", ptr)
    return ("id", id(obj))


def get_id(obj, prefix):
    """Return the stable id for obj, creating one on first sight.

    Same object (by LOM identity) always returns the same string.
    Different objects always get different strings, even across prefixes.
    """
    key = _identity(obj)
    existing = _ids.get(key)
    if existing is not None:
        return existing
    next_n = _counters.get(prefix, 0) + 1
    _counters[prefix] = next_n
    stable_id = "%s:%d" % (prefix, next_n)
    _ids[key] = stable_id
    _keepalive[key] = obj
    return stable_id


def find(objects, stable_id, prefix):
    """Return the index of the object in `objects` whose stable id is
    `stable_id`, or None if not found. `prefix` is used as a cheap sanity
    filter (a stable_id for a different prefix can never match)."""
    if not stable_id or not stable_id.startswith(prefix + ":"):
        return None
    for index, obj in enumerate(objects):
        if _ids.get(_identity(obj)) == stable_id:
            return index
    return None


def clear():
    """Reset all registered ids and counters (new Live set / test isolation)."""
    _ids.clear()
    _counters.clear()
    _keepalive.clear()
