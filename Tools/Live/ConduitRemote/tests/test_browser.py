"""BrowserService (M4): roots/children per node-id, load, preview."""

import pytest

from ConduitRemote.browser import BrowserService, LIST_ADDRESS
from ConduitRemote.handlers.registry import CommandContext, CommandRegistry
from ConduitRemote.handlers import song as song_handlers
from ConduitRemote.sync import stable_ids
from ConduitRemote.tests.stub_live import (Browser, BrowserItem, FakeSender,
                                           Song)


def make_service():
    kick = BrowserItem("Kick.adg", is_loadable=True)
    snare = BrowserItem("Snare.adg", is_loadable=True)
    acoustic = BrowserItem("Acoustic", [kick, snare])
    drums = BrowserItem("Drums", [acoustic])
    sounds = BrowserItem("Sounds", [BrowserItem("Pad.adg", is_loadable=True)])
    browser = Browser(sounds=sounds, drums=drums)

    sender = FakeSender()
    service = BrowserService(lambda: browser, sender)
    return service, sender, browser, drums, acoustic, kick


def items_of(sender):
    address, _seq, payload = sender.last()
    assert address == LIST_ADDRESS
    return payload


def test_roots_registers_available_roots_in_order():
    service, sender, _browser, _drums, _ac, _kick = make_service()

    service.handle_roots(LIST_ADDRESS, [])
    payload = items_of(sender)

    assert payload["p"] == 0
    names = [entry[1] for entry in payload["it"]]
    assert names == ["Sounds", "Drums"]   # nur vorhandene Wurzeln, Reihenfolge fix
    assert all(entry[2] == 1 for entry in payload["it"])   # Ordner-Flag


def test_children_walk_and_flags():
    service, sender, _browser, drums, acoustic, kick = make_service()
    service.handle_roots(LIST_ADDRESS, [])
    drums_id = [e[0] for e in items_of(sender)["it"] if e[1] == "Drums"][0]

    service.handle_children(LIST_ADDRESS, [drums_id])
    payload = items_of(sender)
    assert payload["p"] == drums_id
    assert payload["it"][0][1] == "Acoustic"

    acoustic_id = payload["it"][0][0]
    service.handle_children(LIST_ADDRESS, [acoustic_id])
    payload = items_of(sender)
    names = [e[1] for e in payload["it"]]
    assert names == ["Kick.adg", "Snare.adg"]
    assert payload["it"][0][2] == 0   # kein Ordner
    assert payload["it"][0][3] == 1   # loadable


def test_children_of_unknown_node_answers_empty():
    service, sender, _browser, _d, _a, _k = make_service()
    service.handle_children(LIST_ADDRESS, [999])
    payload = items_of(sender)
    assert payload["p"] == 999
    assert payload["it"] == []


def test_load_and_preview_reach_live_browser():
    service, sender, browser, drums, acoustic, kick = make_service()
    service.handle_roots(LIST_ADDRESS, [])
    drums_id = [e[0] for e in items_of(sender)["it"] if e[1] == "Drums"][0]
    service.handle_children(LIST_ADDRESS, [drums_id])
    acoustic_id = items_of(sender)["it"][0][0]
    service.handle_children(LIST_ADDRESS, [acoustic_id])
    kick_id = items_of(sender)["it"][0][0]

    service.handle_load(LIST_ADDRESS, [kick_id])
    assert browser.loaded == [kick]

    service.handle_preview(LIST_ADDRESS, [kick_id])
    assert browser.previewed == [kick]

    service.handle_stop_preview(LIST_ADDRESS, [])
    assert browser.preview_stopped == 1

    # Unbekannte IDs: still ignoriert
    service.handle_load(LIST_ADDRESS, [4711])
    assert browser.loaded == [kick]


def test_roots_survive_missing_browser():
    sender = FakeSender()
    service = BrowserService(lambda: None, sender)
    service.handle_roots(LIST_ADDRESS, [])
    assert items_of(sender)["it"] == []


def test_selected_track_command_targets_song_view():
    stable_ids.clear()
    song = Song(num_tracks=2, num_scenes=2)
    registry = CommandRegistry()
    song_handlers.register_all(registry)

    def resolver(ref):
        index = stable_ids.find(song.tracks, ref, stable_ids.TRACK_PREFIX)
        return song.tracks[index] if index is not None else None

    ctx = CommandContext(lambda: song, track_resolver=resolver)

    registry.dispatch("/live/song/set/selected_track", [1], ctx)
    assert song.view.selected_track is song.tracks[1]

    tid = stable_ids.get_id(song.tracks[0], stable_ids.TRACK_PREFIX)
    registry.dispatch("/live/song/set/selected_track", [tid], ctx)
    assert song.view.selected_track is song.tracks[0]
    stable_ids.clear()
