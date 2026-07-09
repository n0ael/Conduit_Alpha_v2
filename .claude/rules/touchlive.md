---
paths:
  - "Source/TouchLive/**"
  - "Source/UI/TouchLivePage/**"
  - "Tests/TouchLive/**"
  - "Tests/UI/TouchLivePageTests.cpp"
  - "Tools/Live/ConduitRemote/**"
  - "docs/TouchLive.md"
---

# Rule: touchlive — Ableton-Live-Remote (docs/TouchLive.md)

**Pflichtlektüre: docs/TouchLive.md** (Spezifikation, Meilensteinleiter,
M1b-Implementierungs-Notizen). Gegenseite: Remote Script
`Tools/Live/ConduitRemote/` (README = Wire-Protokoll).

Architektur (§3):
- Reiner Message-Thread-Code — der **Audio-Thread ist an der Remote NIE
  beteiligt**. Netzwerk-Thread → Queue → AsyncUpdater → Modell.
- `TouchLiveClient` hat einen EIGENEN OSCReceiver/OSCSender — KOMPLETT
  getrennt von OscController/OscSendService.
- **Ports: Command 9010 (→ Script), Listen 9011 (← Script)** — bewusst
  getrennt von Conduits eigenem OSC (9000/9001) und AbletonOSC (11000/11001).
- `LiveSetModel`: **KEIN UndoManager** (Undo ist Lives Sache,
  `/live/song/undo` als Command); wird **NIE serialisiert** (Stable-IDs der
  Gegenseite sind Laufzeit-IDs, CLAUDE.md §6). Mutationen nur Message Thread.
- Reconnect-Snapshot wird als **Tree-DIFF** angewendet (kein Clear+Rebuild) —
  UI-Listener dürfen nicht flackern. var-Falle: DynamicObjects vergleichen
  über Pointer → IMMER deep vergleichen VOR `setProperty`.

Feel-Regeln (§5.1, verbindlich):
- **Echo-Suppression:** berührte Keys (Schema Domain + Stable-ID + Feld,
  `makeParameterKey`) verwerfen eingehende Diffs, Touch + 250 ms Nachlauf.
  Das UI wartet NIE auf Live-Feedback, um sich zu bewegen.
- **Touch-Thinning:** `sendTouchValue` max. ein Send pro ~16 ms pro Adresse,
  letzter Wert gewinnt; kein Bundling mit Struktur-Traffic.
- **Fremd-Feedback slewt** (~30 ms, AnimatedValue), nie hart setzen —
  Meter ausgenommen (roh, M2).

UI (M1c, Source/UI/TouchLivePage/):
- Page-Slot 2 (Icon `pageTouchLive`, User-SVG) — die Clip-Page bekommt
  später wieder einen Slot. Sub-Tabs GRID · MIXER · DEVICE · BROWSER.
- Fader-Drag ist RELATIV (kein Cap-Sprung), Doppeltipp = 0 dB; Wert↔dB via
  `touchlive::faderscale` (Näherung — nach Feldtest kalibrieren).
- Mixer-Feldnamen der Gegenseite: `vol`/`pan`/`sends`/`mute`/`solo`/`arm`.
  Returns nur über `/live/return/set/*` (Index), Master `/live/master/set/*`
  — der Stable-ID-Resolver der Gegenseite kennt NUR reguläre Tracks.
- Kanalbreite (TouchLiveSettings) steuert Mixer-Züge UND Grid-Spalten.

Wire-Protokoll (Gegenseite M1a):
- `/remote/state/{d}/snapshot|diff` mit `[seq:int, chunk:int, chunks:int,
  json:str]`; Diff: Key → kompletter neuer Wert, entfernt → null.
- Seq-Lücke pro Domain → `/get` (Re-Request gedrosselt, Heartbeat-Tick heilt
  verlorene Requests); Chunks gleicher seq erst KOMPLETT sammeln.
- Heartbeat `/remote/ping` ↔ `/remote/pong` alle 2 s; 3 verpasste Pongs →
  disconnected; Ping-Kadenz = Reconnect-Backoff; JEDER Übergang zu connected
  subscribed neu.
- OSC-Codec der Gegenseite encodiert Python-Bools als T/F-Tags — juce_osc
  kennt die NICHT. Richtung Script→Conduit nie Bool-Argumente senden
  (Bools reisen im JSON).
