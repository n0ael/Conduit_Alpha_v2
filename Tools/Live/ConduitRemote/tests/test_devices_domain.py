"""DevicesDomain (M3): Ketten, Parameter-Meta/Werte, Diff-Granularitaet."""

import pytest

from ConduitRemote.handlers import device as device_handlers
from ConduitRemote.handlers.registry import CommandContext, CommandRegistry
from ConduitRemote.sync import stable_ids
from ConduitRemote.sync.devices import DevicesDomain
from ConduitRemote.tests.stub_live import (Device, DeviceParameter, FakeSender,
                                           Song)


@pytest.fixture(autouse=True)
def _clear_registry():
    stable_ids.clear()
    yield
    stable_ids.clear()


def make_song_with_devices():
    song = Song(num_tracks=2, num_scenes=2, num_sends=2)
    eq = Device("EQ Eight", "Eq8", [
        DeviceParameter("Device On", 1.0, 0.0, 1.0),
        DeviceParameter("1 Frequency A", 0.5),
        DeviceParameter("1 Gain A", 0.0, -1.0, 1.0),
    ])
    comp = Device("Compressor", "Compressor2", [
        DeviceParameter("Device On", 1.0, 0.0, 1.0),
        DeviceParameter("Ratio", 0.4),
        DeviceParameter("Model", 0.0, 0.0, 2.0, is_quantized=True,
                        value_items=["Peak", "RMS", "Expand"]),
    ])
    song.tracks[0].devices.append(eq)
    song.tracks[0].devices.append(comp)
    return song, eq, comp


def make_domain():
    song, eq, comp = make_song_with_devices()
    sender = FakeSender()
    domain = DevicesDomain(song, sender)
    return song, sender, domain, eq, comp


def test_snapshot_shape():
    song, sender, domain, eq, comp = make_domain()
    domain.on_subscribe()
    payload = sender.last()[2]

    tid = stable_ids.get_id(song.tracks[0], stable_ids.TRACK_PREFIX)
    eq_id = stable_ids.get_id(eq, stable_ids.DEVICE_PREFIX)
    comp_id = stable_ids.get_id(comp, stable_ids.DEVICE_PREFIX)

    assert payload["chain:" + tid] == [eq_id, comp_id]
    assert payload["dev:" + eq_id]["name"] == "EQ Eight"
    assert payload["dev:" + eq_id]["class_name"] == "Eq8"
    assert payload["dev:" + eq_id]["is_active"] is True

    meta = payload["parmeta:" + eq_id]
    assert meta[1]["name"] == "1 Frequency A"
    assert meta[2]["min"] == -1.0
    assert "items" not in meta[1]

    # Quantisierte Parameter tragen ihre Werteliste
    comp_meta = payload["parmeta:" + comp_id]
    assert comp_meta[2]["quant"] is True
    assert comp_meta[2]["items"] == ["Peak", "RMS", "Expand"]

    vals = payload["parvals:" + eq_id]
    assert vals == [1.0, 0.5, 0.0]

    # Ketten existieren auch fuer Returns/Master (leer)
    rt = stable_ids.get_id(song.return_tracks[0], stable_ids.RETURN_PREFIX)
    assert payload["chain:" + rt] == []


def test_value_change_diffs_only_parvals_row():
    song, sender, domain, eq, _comp = make_domain()
    domain.on_subscribe()
    sender.clear()

    eq.parameters[1].value = 0.75
    domain.on_tick(1)

    address, _seq, diff = sender.last()
    assert address == "/remote/state/devices/diff"
    eq_id = stable_ids.get_id(eq, stable_ids.DEVICE_PREFIX)
    assert list(diff.keys()) == ["parvals:" + eq_id]   # NUR die heisse Zeile
    assert diff["parvals:" + eq_id] == [1.0, 0.75, 0.0]


def test_device_add_and_remove_diff_chain():
    song, sender, domain, eq, comp = make_domain()
    domain.on_subscribe()
    sender.clear()

    delay = Device("Delay", "Delay", [DeviceParameter("Device On", 1.0)])
    song.tracks[1].devices.append(delay)
    song.tracks[1].notify("devices")
    domain.on_tick(1)

    diff = sender.last()[2]
    t1 = stable_ids.get_id(song.tracks[1], stable_ids.TRACK_PREFIX)
    delay_id = stable_ids.get_id(delay, stable_ids.DEVICE_PREFIX)
    assert diff["chain:" + t1] == [delay_id]
    assert diff["dev:" + delay_id]["name"] == "Delay"

    sender.clear()
    song.tracks[0].devices.remove(comp)
    song.tracks[0].notify("devices")
    domain.on_tick(2)

    diff = sender.last()[2]
    comp_id = stable_ids.get_id(comp, stable_ids.DEVICE_PREFIX)
    t0 = stable_ids.get_id(song.tracks[0], stable_ids.TRACK_PREFIX)
    eq_id = stable_ids.get_id(eq, stable_ids.DEVICE_PREFIX)
    assert diff["chain:" + t0] == [eq_id]
    assert diff["dev:" + comp_id] is None       # Keys verschwinden -> null
    assert diff["parvals:" + comp_id] is None


def test_is_active_toggle_diffs_dev_entry():
    song, sender, domain, eq, _comp = make_domain()
    domain.on_subscribe()
    sender.clear()

    eq.is_active = False
    domain.on_tick(1)

    diff = sender.last()[2]
    eq_id = stable_ids.get_id(eq, stable_ids.DEVICE_PREFIX)
    assert diff["dev:" + eq_id]["is_active"] is False


def test_new_parameter_value_arrives_via_listener_after_device_add():
    song, sender, domain, _eq, _comp = make_domain()
    domain.on_subscribe()

    delay = Device("Delay", "Delay", [DeviceParameter("Device On", 1.0),
                                      DeviceParameter("Time", 0.2)])
    song.tracks[1].devices.append(delay)
    song.tracks[1].notify("devices")
    domain.on_tick(1)
    sender.clear()

    # Der Rebind nach dem Add muss auch die NEUEN Parameter belauschen
    delay.parameters[1].value = 0.9
    domain.on_tick(2)

    delay_id = stable_ids.get_id(delay, stable_ids.DEVICE_PREFIX)
    assert sender.last()[2]["parvals:" + delay_id] == [1.0, 0.9]


# -- Commands -------------------------------------------------------------------

def make_command_ctx(song):
    def resolve_device(ref):
        for track in (list(song.tracks) + list(song.return_tracks)
                      + [song.master_track]):
            for dev in track.devices:
                if stable_ids.get_id(dev, stable_ids.DEVICE_PREFIX) == ref:
                    return dev
        return None

    registry = CommandRegistry()
    device_handlers.register_all(registry)
    ctx = CommandContext(lambda: song, device_resolver=resolve_device)
    return registry, ctx


def test_set_parameter_command_via_stable_id():
    song, eq, _comp = make_song_with_devices()
    registry, ctx = make_command_ctx(song)
    eq_id = stable_ids.get_id(eq, stable_ids.DEVICE_PREFIX)

    registry.dispatch("/live/device/set/parameter", [eq_id, 1, 0.66], ctx)
    assert eq.parameters[1].value == pytest.approx(0.66)

    # unbekannte Referenz / Index ausser Range: still ignoriert
    registry.dispatch("/live/device/set/parameter", ["dv:999", 1, 0.1], ctx)
    registry.dispatch("/live/device/set/parameter", [eq_id, 99, 0.1], ctx)
    assert eq.parameters[1].value == pytest.approx(0.66)


def test_set_is_active_command():
    song, eq, _comp = make_song_with_devices()
    registry, ctx = make_command_ctx(song)
    eq_id = stable_ids.get_id(eq, stable_ids.DEVICE_PREFIX)

    registry.dispatch("/live/device/set/is_active", [eq_id, 0], ctx)
    assert eq.is_active is False
    registry.dispatch("/live/device/set/is_active", [eq_id, 1], ctx)
    assert eq.is_active is True
