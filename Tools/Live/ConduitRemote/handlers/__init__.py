"""Command handlers: OSC address -> Live Object Model actions.

Each submodule (song, track, session) exposes register_all(registry) which
wires its addresses into a shared handlers.registry.CommandRegistry.

FAST_WHITELIST lists the addresses that are safe to apply directly from the
OSC receiver thread (see osc/server.py / config.FAST_APPLY): pure LOM
value-writes only, nothing structural or with side effects beyond the one
parameter being set.
"""

FAST_WHITELIST = (
    "/live/track/set/volume",
    "/live/track/set/panning",
    "/live/track/set/send",
    "/live/return/set/volume",
    "/live/return/set/panning",
    "/live/master/set/volume",
    "/live/master/set/panning",
    "/live/device/set/parameter",
)
