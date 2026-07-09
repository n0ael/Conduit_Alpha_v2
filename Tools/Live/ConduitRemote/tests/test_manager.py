import socket
import time

import pytest

from ConduitRemote import config
from ConduitRemote.manager import Manager
from ConduitRemote.osc import codec
from ConduitRemote.tests.stub_live import Song

HOST = "127.0.0.1"


def poll_until(predicate, timeout=1.0, interval=0.02):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if predicate():
            return True
        time.sleep(interval)
    return predicate()


class FakeCInstance(object):
    """Manager only touches c_instance.song() when no song kwarg is given;
    tests always inject a stub song, so this stays empty."""
    pass


class DummyDomain(object):
    def __init__(self, name="dummy", fail_on_tick=False):
        self.NAME = name
        self.subscribed = False
        self.subscribe_calls = 0
        self.unsubscribe_calls = 0
        self.snapshot_calls = 0
        self.tick_calls = []
        self.detached = False
        self._fail_on_tick = fail_on_tick

    def on_subscribe(self):
        self.subscribed = True
        self.subscribe_calls += 1

    def on_unsubscribe(self):
        self.subscribed = False
        self.unsubscribe_calls += 1

    def send_snapshot(self):
        self.snapshot_calls += 1

    def on_tick(self, tick_index):
        self.tick_calls.append(tick_index)
        if self._fail_on_tick:
            raise RuntimeError("boom")

    def detach(self):
        self.detached = True


@pytest.fixture
def response_listener():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((HOST, config.RESPONSE_PORT))
    sock.settimeout(1.0)
    yield sock
    sock.close()


class FakeTimer(object):
    """Live.Base.Timer-Ersatz: Tests feuern fire() von Hand."""

    def __init__(self, callback):
        self.callback = callback
        self.started = False
        self.stopped = False

    def start(self):
        self.started = True

    def stop(self):
        self.stopped = True

    def fire(self):
        self.callback()


def make_manager(domain_factory, timer_factory=None):
    song = Song(num_tracks=2, num_scenes=4, num_sends=2)
    c_instance = FakeCInstance()
    manager = Manager(c_instance, song=song, domain_factory=domain_factory,
                      timer_factory=timer_factory)
    return manager, song


def send_to_manager(address, args=()):
    client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    client.sendto(codec.encode_message(address, args), (HOST, config.LISTEN_PORT))
    client.close()


def test_subscribe_reaches_dummy_domain():
    domain = DummyDomain()
    manager, _song = make_manager(lambda song, sender: {"dummy": domain})
    try:
        send_to_manager("/remote/state/dummy/subscribe", [])
        assert poll_until(lambda: (manager.tick(), domain.subscribe_calls == 1)[1])
        assert domain.subscribed is True
    finally:
        manager.disconnect()


def test_get_triggers_snapshot():
    domain = DummyDomain()
    manager, _song = make_manager(lambda song, sender: {"dummy": domain})
    try:
        send_to_manager("/remote/state/dummy/get", [])
        assert poll_until(lambda: (manager.tick(), domain.snapshot_calls == 1)[1])
    finally:
        manager.disconnect()


def test_fast_timer_pumps_whitelisted_writes_between_ticks():
    """Fast-Path v2: der Timer-Pump wendet Volume-Writes an, OHNE dass
    manager.tick() laeuft (frueher: 100-ms-Stufen, Feldtest 09.07.2026)."""
    timers = []

    def factory(callback):
        timer = FakeTimer(callback)
        timer.start()
        timers.append(timer)
        return timer

    manager, song = make_manager(lambda song_, sender: {}, timer_factory=factory)
    try:
        assert len(timers) == 1
        assert timers[0].started
        assert manager.server.pump_active is True

        send_to_manager("/live/track/set/volume", [0, 0.25])

        applied = poll_until(
            lambda: (timers[0].fire(),
                     abs(song.tracks[0].mixer_device.volume.value - 0.25) < 1e-6)[1])
        assert applied   # kein manager.tick() noetig
    finally:
        manager.disconnect()
        assert timers[0].stopped


def test_meters_stream_at_pump_rate(response_listener):
    """Meter haengen im Timer-Modus an der Pump-Kadenz (~33 Hz), nicht am
    100-ms-Tick (User-Feedback 09.07.2026: Meter so fluessig wie Fader)."""
    timers = []

    def factory(callback):
        timer = FakeTimer(callback)
        timers.append(timer)
        return timer

    manager, song = make_manager(lambda song_, sender: {}, timer_factory=factory)
    try:
        song.tracks[0].output_meter_left = 0.5

        # client_addr lernen + Meter abonnieren (non-fast -> tick)
        send_to_manager("/remote/ping", [])
        send_to_manager("/remote/meters/subscribe", [])
        assert poll_until(lambda: (timers[0].fire(), manager.tick(),
                                   manager.meters.subscribed)[2])

        # METER_PUMP_DIVIDER Pumps ohne tick() -> ein Meter-Frame
        for _ in range(config.METER_PUMP_DIVIDER):
            timers[0].fire()

        def got_meter_frame():
            try:
                data, _ = response_listener.recvfrom(65535)
            except socket.timeout:
                return False
            return any(address == "/remote/meters"
                       for address, _args in codec.decode_packet(data))

        assert poll_until(got_meter_frame, timeout=2.0)
    finally:
        manager.disconnect()


def test_unsubscribe_reaches_dummy_domain():
    domain = DummyDomain()
    manager, _song = make_manager(lambda song, sender: {"dummy": domain})
    try:
        domain.on_subscribe()
        send_to_manager("/remote/state/dummy/unsubscribe", [])
        assert poll_until(lambda: (manager.tick(), domain.unsubscribe_calls == 1)[1])
        assert domain.subscribed is False
    finally:
        manager.disconnect()


def test_ping_gets_pong(response_listener):
    domain = DummyDomain()
    manager, _song = make_manager(lambda song, sender: {"dummy": domain})
    try:
        send_to_manager("/remote/ping", [])

        pong = {}

        def try_recv():
            try:
                response_listener.settimeout(0.05)
                data, _addr = response_listener.recvfrom(65535)
                pong["msg"] = codec.decode_message(data)
                return True
            except socket.timeout:
                return False

        got = False
        deadline = time.time() + 1.0
        while time.time() < deadline and not got:
            manager.tick()
            got = try_recv()

        assert got is True
        address, args = pong["msg"]
        assert address == "/remote/pong"
        assert args == [config.PROTOCOL_VERSION]
        assert manager.heartbeat.alive is True
    finally:
        manager.disconnect()


def test_track_command_applies_to_stub_song():
    domain = DummyDomain()
    manager, song = make_manager(lambda song, sender: {"dummy": domain})
    try:
        send_to_manager("/live/track/set/volume", [0, 0.25])
        assert poll_until(
            lambda: (manager.tick(),
                     song.tracks[0].mixer_device.volume.value == pytest.approx(0.25))[1])
    finally:
        manager.disconnect()


def test_domain_dropped_after_six_consecutive_failures():
    domain = DummyDomain(fail_on_tick=True)
    manager, _song = make_manager(lambda song, sender: {"dummy": domain})
    try:
        for _ in range(6):
            manager.tick()
        assert "dummy" not in manager.domains
        assert domain.detached is True
        assert len(domain.tick_calls) == 6
    finally:
        manager.disconnect()


def test_domain_survives_fewer_than_six_failures():
    domain = DummyDomain(fail_on_tick=True)
    manager, _song = make_manager(lambda song, sender: {"dummy": domain})
    try:
        for _ in range(5):
            manager.tick()
        assert "dummy" in manager.domains
        assert domain.detached is False
    finally:
        manager.disconnect()


def test_disconnect_shuts_down_and_releases_port():
    domain = DummyDomain()
    manager, _song = make_manager(lambda song, sender: {"dummy": domain})
    manager.disconnect()

    assert domain.unsubscribe_calls >= 1
    assert domain.detached is True

    # port must be free again
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    probe.bind((config.BIND_HOST, config.LISTEN_PORT))
    probe.close()


def test_schedule_message_kicks_off_first_tick():
    domain = DummyDomain()
    manager, _song = make_manager(lambda song, sender: {"dummy": domain})
    try:
        assert manager.scheduled  # ControlSurface stub recorded schedule_message calls
        delay, fn = manager.scheduled[0]
        assert delay == 1
        assert fn == manager.tick
    finally:
        manager.disconnect()
