"""ConduitRemote configuration.

All tunables in one place. Values chosen per ABLETON-REMOTE.md (July 2026).
"""

# --- Network -----------------------------------------------------------------
# Deliberately distinct from Conduit's own OSC (9000/9001) and from
# AbletonOSC's defaults (11000/11001) so everything can run in parallel.
LISTEN_PORT = 9010          # script listens here (commands from Conduit)
RESPONSE_PORT = 9011        # replies/pushes go to sender_ip:RESPONSE_PORT
BIND_HOST = "0.0.0.0"       # wired-LAN setup: Conduit runs on another machine

# --- Fast path ---------------------------------------------------------------
# When True, whitelisted pure-value writes (track volume/pan/send) are
# applied at high rate.  HOW changed on 2026-07-09 (field test): a receiver
# THREAD is useless inside Live - its embedded Python only schedules
# background threads at the ~100 ms tick (GIL stays with the host), which
# is exactly why touchAble/Grip faders step at ~10 Hz.  Instead the manager
# drives OscServer.pump() from a Live.Base.Timer (C++-side timer firing
# Python callbacks on the MAIN thread, AbletonJS/ClyphX-Pro pattern): the
# socket is drained ~100x/s and whitelisted writes apply immediately and
# LOM-safe.  If Live.Base.Timer is unavailable, everything falls back to
# the ~100 ms tick (still correct, just coarse).
FAST_APPLY = True
FAST_TIMER_INTERVAL_MS = 10   # pump()-Kadenz; Anwendung limitiert Conduits 60-Hz-Thinning

# --- Rates & limits ----------------------------------------------------------
TICK_INTERVAL = 1           # Live scheduler ticks between manager ticks (~100 ms)
METER_TICK_DIVIDER = 1      # tick-rate fallback: meters every N manager ticks (~10 Hz)
METER_PUMP_DIVIDER = 3      # timer path: meters every N pumps (3 x 10 ms ~= 33 Hz)
MAX_OSC_PAYLOAD_BYTES = 9000  # keep UDP datagrams well under typical MTU limits
HEARTBEAT_TIMEOUT_TICKS = 60  # ~6 s without /remote/ping -> client considered gone

# --- Protocol ----------------------------------------------------------------
PROTOCOL_VERSION = 1
DOMAINS = ("transport", "tracks", "mixer", "session")
