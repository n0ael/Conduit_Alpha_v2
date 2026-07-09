# ConduitRemote

OSC Remote Script für Ableton Live 11/12 — Gegenstück zur TouchLive-Page in Conduit. Meilenstein **M1a** aus `TOUCHLIVE.md`: Transport, Tracks,
Mixer, Session (Clip-Grid) mit Push-Sync und Fast-Path.

## Warum nicht einfach AbletonOSC?

Alle verbreiteten OSC-Scripts (AbletonOSC, touchAble, Grip …) wenden
eingehende Fader-Werte effektiv nur im ~100-ms-Scheduler-Tick von Live an
(**10 Hz**, daher das bekannte Ruckeln) — auch die mit eigenem
Empfangs-Thread: Lives embedded Python schedult Background-Threads
praktisch nur im Tick (GIL bleibt beim Host; per Automations-Aufnahme
gemessen, 111-ms-Stufen, 09.07.2026). ConduitRemote hat deshalb einen
**Fast-Path über `Live.Base.Timer`**: ein C++-seitiger Timer feuert
`OscServer.pump()` ~alle 10 ms auf dem **Main Thread** (LOM-sicher) —
reine Wert-Schreibungen (Volume/Pan/Send) werden sofort angewendet, alles
Strukturelle bleibt im Tick. Abschaltbar über `FAST_APPLY = False`; ohne
verfügbaren Timer fällt alles automatisch auf Tick-Rate zurück.

## Installation

1. Diesen Ordner `ConduitRemote` kopieren nach:
   - Windows: `\Users\[user]\Documents\Ableton\User Library\Remote Scripts\`
   - macOS: `~/Music/Ableton/User Library/Remote Scripts/`
2. Live neu starten.
3. Preferences → Link/Tempo/MIDI → Control Surface → **ConduitRemote** wählen.

Ports (in `config.py`): lauscht auf **9010**, antwortet an Absender-IP:**9011**.
Bewusst getrennt von Conduits eigenem OSC (9000/9001) und AbletonOSC (11000/11001).

## Protokoll

**Commands (Client → Script)** — AbletonOSC-kompatible Adressen:

| Adresse | Argumente |
|---|---|
| `/live/song/start_playing` · `stop_playing` · `continue_playing` · `undo` · `redo` | — |
| `/live/song/set/tempo` | `f` |
| `/live/song/set/metronome` · `session_record` | `i` |
| `/live/track/set/volume` · `panning` | `track_ref, f` |
| `/live/track/set/send` | `track_ref, i send, f` |
| `/live/track/set/mute` · `solo` · `arm` | `track_ref, i` |
| `/live/return/set/volume` · `panning` | `i return, f` |
| `/live/master/set/volume` · `panning` | `f` |
| `/live/clip_slot/fire` · `stop` | `track_ref, i scene` |
| `/live/scene/fire` | `i scene` |
| `/live/track/stop_all_clips` | `track_ref` |
| `/live/device/set/parameter` | `device_ref, i param, f` |
| `/live/device/set/is_active` | `device_ref, i` |

`device_ref` = Stable-ID-String (`"dv:3"`) aus der devices-Domain.

`track_ref` = Index (int, AbletonOSC-kompatibel) **oder** Stable-ID-String
(`"tr:3"` aus den Snapshots) — Stable-IDs überleben Track-Reorder in Live.

**State (Script → Client)** — Domain-Sync, JSON über OSC:

- `/remote/state/{domain}/get` → Voll-Snapshot anfordern
- `/remote/state/{domain}/subscribe` / `unsubscribe`
- Antworten: `/remote/state/{domain}/snapshot` bzw. `/diff`,
  Argumente `[seq:int, chunk:int, chunks:int, json:str]`
- Diff-Semantik: geänderte Keys → neuer Wert, entfernte Keys → `null`.
  Verlorene Pakete heilt der Client durch Snapshot-Neuanforderung (seq-Lücke).

Domains: `transport` (is_playing, tempo, metronome, session_record, sig) ·
`tracks` (Name, Farbe, Art, Reihenfolge) · `mixer` (vol/pan/sends/mute/solo/arm)
· `session` (Clip-Grid pro Track-Zeile: state stopped/playing/triggered/recording
+ Name/Farbe; Scenes) · `devices` (M3: `chain:{tid}`-Ketten, `dev:{dvid}`-
Struktur, `parmeta:{dvid}`-Metadaten, `parvals:{dvid}`-Werte — nur
Top-Level-Devices).

**Heartbeat:** Client sendet `/remote/ping` (~2 s) → `/remote/pong [version]`.
Nach ~6 s ohne Ping werden alle Subscriptions beendet.

**Meter (M2)** — Hochraten-Pfad, getrennt von den Domains:

- `/remote/meters/subscribe` / `unsubscribe`
- Push: `/remote/meters` mit flachen Tripeln `[id:str, left:f, right:f] × n`
  (Tracks + Returns + Master, gleiche Stable-IDs wie die Domains; Werte =
  Lives rohe `output_meter`-Norm 0..1) — im Timer-Modus ~33 Hz
  (`METER_PUMP_DIVIDER`), ohne Timer ~10 Hz am Tick. Devices mit
  `gain_reduction` (Compressor/Glue/Limiter) hängen `[dv-id, gr, gr]` an.
  Keine Sequenznummer (Frames sind idempotent), Stille wird nach einem
  Null-Frame dedupliziert.

## Tests

```
python3 -m pytest tests/ -q     # 120 Tests, läuft ohne Live (Live-API-Stub)
```

## Lizenz-Hinweise

Eigenständige Implementierung. Das Adress-Schema der Commands folgt bewusst
den Konventionen von [AbletonOSC](https://github.com/ideoforms/AbletonOSC)
(MIT, © Daniel Jones). Kein Code aus proprietären Scripts (touchAble, Grip, HK).

## Status / Roadmap

M1a fertig, Meter-Hochraten-Pfad (M2) und Device-Domain generisch (M3)
drin. Es folgen laut `TOUCHLIVE.md` (im Conduit-Repo: docs/TouchLive.md):
Browser (M4), bespoke Device-UIs (M5), Modulator-Zwillinge (M6).
