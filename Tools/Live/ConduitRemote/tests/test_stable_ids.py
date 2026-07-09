import pytest

from ConduitRemote.sync import stable_ids


@pytest.fixture(autouse=True)
def _clear_registry():
    stable_ids.clear()
    yield
    stable_ids.clear()


class _Obj(object):
    pass


def test_same_object_same_id():
    obj = _Obj()
    first = stable_ids.get_id(obj, stable_ids.TRACK_PREFIX)
    second = stable_ids.get_id(obj, stable_ids.TRACK_PREFIX)
    assert first == second


def test_different_objects_different_ids():
    a, b = _Obj(), _Obj()
    ida = stable_ids.get_id(a, stable_ids.TRACK_PREFIX)
    idb = stable_ids.get_id(b, stable_ids.TRACK_PREFIX)
    assert ida != idb


def test_id_format():
    obj = _Obj()
    sid = stable_ids.get_id(obj, stable_ids.TRACK_PREFIX)
    assert sid == "tr:1"


def test_prefixes_are_independent_counters():
    a, b = _Obj(), _Obj()
    tr_id = stable_ids.get_id(a, stable_ids.TRACK_PREFIX)
    sc_id = stable_ids.get_id(b, stable_ids.SCENE_PREFIX)
    assert tr_id == "tr:1"
    assert sc_id == "sc:1"


def test_reorder_keeps_ids():
    objs = [_Obj(), _Obj(), _Obj()]
    ids = [stable_ids.get_id(o, stable_ids.TRACK_PREFIX) for o in objs]
    # simulate a Live reorder: same objects, new list order
    reordered = [objs[2], objs[0], objs[1]]
    reordered_ids = [stable_ids.get_id(o, stable_ids.TRACK_PREFIX) for o in reordered]
    assert reordered_ids == [ids[2], ids[0], ids[1]]


def test_find_locates_index_after_reorder():
    objs = [_Obj(), _Obj(), _Obj()]
    ids = [stable_ids.get_id(o, stable_ids.TRACK_PREFIX) for o in objs]
    reordered = [objs[2], objs[0], objs[1]]
    assert stable_ids.find(reordered, ids[0], stable_ids.TRACK_PREFIX) == 1
    assert stable_ids.find(reordered, ids[2], stable_ids.TRACK_PREFIX) == 0


def test_find_unknown_id_returns_none():
    objs = [_Obj(), _Obj()]
    for o in objs:
        stable_ids.get_id(o, stable_ids.TRACK_PREFIX)
    assert stable_ids.find(objs, "tr:999", stable_ids.TRACK_PREFIX) is None


def test_find_wrong_prefix_returns_none():
    obj = _Obj()
    sid = stable_ids.get_id(obj, stable_ids.TRACK_PREFIX)
    assert stable_ids.find([obj], sid, stable_ids.SCENE_PREFIX) is None


def test_clear_resets_counters():
    obj = _Obj()
    stable_ids.get_id(obj, stable_ids.TRACK_PREFIX)
    stable_ids.clear()
    obj2 = _Obj()
    assert stable_ids.get_id(obj2, stable_ids.TRACK_PREFIX) == "tr:1"


def test_live_ptr_wins_over_object_identity():
    """Feldtest 09.07.2026: Lives Wrapper sind nicht identitaetsstabil -
    zwei verschiedene Python-Objekte mit gleichem _live_ptr sind DERSELBE
    LOM-Gegenstand und muessen dieselbe Stable-ID bekommen."""
    a, b = _Obj(), _Obj()
    a._live_ptr = 4711
    b._live_ptr = 4711
    ida = stable_ids.get_id(a, stable_ids.TRACK_PREFIX)
    idb = stable_ids.get_id(b, stable_ids.TRACK_PREFIX)
    assert ida == idb


def test_live_ptr_distinguishes_objects():
    a, b = _Obj(), _Obj()
    a._live_ptr = 1
    b._live_ptr = 2
    assert (stable_ids.get_id(a, stable_ids.TRACK_PREFIX)
            != stable_ids.get_id(b, stable_ids.TRACK_PREFIX))


def test_find_matches_via_live_ptr():
    a = _Obj()
    a._live_ptr = 99
    sid = stable_ids.get_id(a, stable_ids.TRACK_PREFIX)

    fresh = _Obj()          # neuer Wrapper, gleicher LOM-Gegenstand
    fresh._live_ptr = 99
    assert stable_ids.find([fresh], sid, stable_ids.TRACK_PREFIX) == 0
