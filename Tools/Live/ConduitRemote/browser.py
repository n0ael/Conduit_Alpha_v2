"""Browser remote (M4): Live's browser tree via load_children pattern.

Deliberately NOT a Domain: the browser tree is huge and lazily built, so
snapshot/diff would be absurd. Pure request/response instead (touchAble
pattern):

    -> /remote/browser/roots                       (no args)
    -> /remote/browser/children  [parent_id:int]
    <- /remote/browser/list      [seq, chunk, chunks, json]
         json = {"p": parent_id, "it": [[id, name, folder, loadable], ...]}
         (parent_id 0 == roots; delivered through delivery.Sender with
          force=True, so the existing chunking covers big folders)

    -> /live/browser/load         [id]   loads onto Live's selected track
    -> /live/browser/preview      [id]
    -> /live/browser/stop_preview

Node ids are handed out per session (registry keeps the BrowserItem alive,
same keepalive idea as stable_ids) - they are runtime-only and must never
be persisted (CLAUDE.md par.6). A lost response simply means the client
re-requests on the next tap; no seq-gap healing needed.

All handlers run on the main thread (tick/timer pump) - LOM-safe. Every
Live access is defensive: missing roots/attributes are skipped, load and
preview failures are logged, never raised.
"""

import logging

logger = logging.getLogger(__name__)

LIST_ADDRESS = "/remote/browser/list"

# Wurzeln in Anzeige-Reihenfolge: (Label, Attributname an Live.Browser)
_ROOTS = (
    ("Sounds", "sounds"),
    ("Drums", "drums"),
    ("Instruments", "instruments"),
    ("Audio Effects", "audio_effects"),
    ("MIDI Effects", "midi_effects"),
    ("Max for Live", "max_for_live"),
    ("Plug-Ins", "plugins"),
    ("Clips", "clips"),
    ("Samples", "samples"),
    ("Packs", "packs"),
    ("User Library", "user_library"),
    ("Current Project", "current_project"),
)


def default_get_browser():
    """App-Pfad: Lives Browser — None ausserhalb Lives (Tests injizieren)."""
    try:
        import Live
        return Live.Application.get_application().browser
    except Exception:
        logger.exception("browser: Live application unavailable")
        return None


class BrowserService(object):
    def __init__(self, get_browser, sender):
        """get_browser: callable -> Live.Browser (lazy, main thread).
        sender: delivery.Sender (chunked JSON transport)."""
        self._get_browser = get_browser
        self._sender = sender
        self._items = {}     # id -> BrowserItem (keepalive, session-only)
        self._labels = {}    # id -> Anzeigename (fuer Roots mit Label)
        self._next_id = 1
        self._seq = 0
        self._roots_ids = None

    # -- handlers (Main Thread) ----------------------------------------------

    def handle_roots(self, address, args):
        browser = self._get_browser()
        if browser is None:
            self._send_list(0, [])
            return

        if self._roots_ids is None:
            self._roots_ids = []
            for label, attr in _ROOTS:
                try:
                    item = getattr(browser, attr)
                except Exception:
                    continue
                if item is None:
                    continue
                node_id = self._register(item)
                self._labels[node_id] = label
                self._roots_ids.append(node_id)

        self._send_list(0, self._roots_ids)

    def handle_children(self, address, args):
        if len(args) < 1:
            return
        item = self._items.get(_as_int(args[0]))
        if item is None:
            logger.debug("browser children: unknown node %r", args)
            self._send_list(_as_int(args[0]), [])
            return

        child_ids = []
        try:
            for child in item.children:
                child_ids.append(self._register(child))
        except Exception:
            logger.exception("browser children failed")

        self._send_list(_as_int(args[0]), child_ids)

    def handle_load(self, address, args):
        if len(args) < 1:
            return
        item = self._items.get(_as_int(args[0]))
        browser = self._get_browser()
        if item is None or browser is None:
            return
        try:
            browser.load_item(item)
        except Exception:
            logger.exception("browser load failed")

    def handle_preview(self, address, args):
        if len(args) < 1:
            return
        item = self._items.get(_as_int(args[0]))
        browser = self._get_browser()
        if item is None or browser is None:
            return
        try:
            browser.preview_item(item)
        except Exception:
            logger.exception("browser preview failed")

    def handle_stop_preview(self, address, args):
        browser = self._get_browser()
        if browser is None:
            return
        try:
            browser.stop_preview()
        except Exception:
            logger.exception("browser stop_preview failed")

    def register_handlers(self, server):
        server.add_handler("/remote/browser/roots", self.handle_roots)
        server.add_handler("/remote/browser/children", self.handle_children)
        server.add_handler("/live/browser/load", self.handle_load)
        server.add_handler("/live/browser/preview", self.handle_preview)
        server.add_handler("/live/browser/stop_preview", self.handle_stop_preview)

    # -- internals -------------------------------------------------------------

    def _register(self, item):
        node_id = self._next_id
        self._next_id += 1
        self._items[node_id] = item
        return node_id

    def _send_list(self, parent_id, node_ids):
        entries = []
        for node_id in node_ids:
            item = self._items.get(node_id)
            if item is None:
                continue
            name = self._labels.get(node_id) or _safe_name(item)
            entries.append([node_id, name,
                            1 if _flag(item, "is_folder") else 0,
                            1 if _flag(item, "is_loadable") else 0])

        self._seq += 1
        self._sender.send_json(LIST_ADDRESS, self._seq,
                               {"p": parent_id, "it": entries}, force=True)


def _as_int(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return -1


def _safe_name(item):
    try:
        return str(item.name)
    except Exception:
        return "?"


def _flag(item, attr):
    try:
        return bool(getattr(item, attr))
    except Exception:
        return False
