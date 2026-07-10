# TouchLive — Ableton-Live-Remote für Conduit — Spezifikation
> Stand: 08.07.2026 — angepasst an CLAUDE.md v5.0 (Dossier-System).
> Subsystem-Name: **TouchLive** (User-Entscheidung 08.07.2026 — Ω ist der
> MPE-Grid-Controller, daher eigener Name gegen Verwechslung).
> Funktionsumfang orientiert an touchAble Pro — ohne XY-Pads / MIDI-Keyboard.
> Wandert bei M1b als Dossier `docs/TouchLive.md` ins Repo (§9).
>
> **Hinweis 08.07.2026:** Ω ist inzwischen der Grid-Touch-Controller
> (docs/Grid.md, ADR 002) — die Live-Remote heißt jetzt TouchLive und bekommt eine EIGENE Page
> im PageHost (Icon/Slot-Entscheidung bei M1c). Alte Ω-Referenzen in
> diesem Dokument sind entsprechend zu lesen.

---

## 1. Ziel & Scope

- Eine eigene TouchLive-Page im PageHost: **eine Page, vier Sub-Tabs**
  — GRID · MIXER · DEVICE · BROWSER. („GRID" meint hier das Clip-Grid
  der Session-View, nicht Conduits Grid-Touch-Controller Ω.)
- **M1 = GRID + MIXER + Transport.** DEVICE und BROWSER sind eigene Meilensteine
  (Sub-Tabs existieren ab Tag 1 als gestylte Platzhalter, Muster PageHost).
- Layout bleibt **nah an Abletons Session-View/Mixer**, touch-optimiert
  (44-px-Targets, keine gestauchte Schrift — CLAUDE.md 10.0).
- Abgrenzung: die geplante Conduit-Mixer-Page (∥∥) ist der INTERNE Mixer
  (Capture-Kanäle). Der Live-Mixer lebt ausschließlich hier unter Ω.

## 2. Backend: „ConduitRemote" Remote Script

Eigenes MIDI Remote Script für Live 11/12, Python, ein Ordner `Tools/Live/ConduitRemote/`.

**Basis & Vorbilder (Bestandsaufnahme der vorliegenden Scripts):**

| Script | Lizenz | Verwendung |
|---|---|---|
| AbletonOSC | MIT | **Code-Basis**: OSC-Server, Handler-Struktur, LOM-Adressschema |
| touchAble Pro | LGPL 2.1 | **Funktions-Referenz**: Browser-API (`/browser/load/*`, `load_children`, Preview), Echtzeit-Meter-Kanal, Listener-Vollausbau |
| Grip | proprietär (© Adam Baker) | **NUR Architektur-Vorbild, kein Code**: Domain-Sync (subscribe → Snapshot → Diffs), stabile IDs, Dedupe + Safety-Resend, 9000-Byte-Payload-Grenze |
| HK Live Remote | nur .pyc | keine Referenz (kein Quellcode) |
| AbleLoop (AbletonJS), ClyphX | — | anderes Modell / Action-Strings — vorerst irrelevant |

**Lizenz-Regel (verbindlich):** aus Grip und HK wird NIE Code übernommen.
AbletonOSC-Code (MIT) frei nutzbar mit Attribution. touchAble-Muster werden
frisch implementiert (Konzepte sind frei, LGPL-Code bleibt draußen).

**Sync-Architektur (Grip-Muster, eigene Implementierung):**

- **Domains**: `transport` · `tracks` · `mixer` · `session` (Clips/Scenes) ·
  `devices` · `browser`. Pro Domain: `/remote/state/{domain}/get` (Voll-Snapshot),
  `/subscribe`, `/unsubscribe`; danach pusht das Script Diffs.
- **Stabile IDs** für Tracks/Clips/Devices (Session-Lebensdauer, script-seitig
  vergeben) — Reihenfolgen-Änderungen in Live zerreißen keine Client-Referenzen.
- **Commands** (Client → Script) folgen dem AbletonOSC-Schema:
  `/live/clip_slot/fire`, `/live/track/set/volume`, `/live/song/start_playing` …
  Feedback läuft NUR über die Domains (kein Request/Response-Mix).
- **Meter/Peaks**: eigener Hochraten-Pfad (bis ~15 Hz gebündelt, ein Bundle pro
  Tick, alle Tracks), getrennt von Struktur-Diffs.
- **Touch-Pfad (Hochrate)**: der gerade berührte Parameter läuft an den Domains
  vorbei — Command-Sends bis 60 Hz (thinned: nur letzter Wert pro Tick),
  Script wendet sofort an. Netzwerk-Annahme: **Kabel-LAN** (User-Entscheidung
  07/2026) — keine Jitter-Puffer, minimale Glättung.
- **Ports**: Script lauscht **9010**, antwortet an **9011** — bewusst getrennt
  von Conduits eigenem OSC (9000/9001) und AbletonOSC-Default (11000/11001),
  damit alles parallel laufen kann.
- Heartbeat `/remote/ping` ↔ `/remote/pong` (2 s), Client gilt nach 3 Misses
  als getrennt → Script stellt Push ein.

## 3. Conduit-Seite: Architektur

**Reiner Message-Thread-Code — der Audio-Thread ist an der Remote NIE beteiligt.**

```
Source/TouchLive/
  TouchLiveClient   Netzwerk (eigener OSCReceiver/Sender, getrennt vom
                        OscController), Connect/Reconnect, Heartbeat, IP+Port
                        in TouchLiveSettings (App-Zustand, Muster MeterSettings;
                        IP-Learn-Muster aus 7.3 wiederverwenden)
  LiveSetModel          ValueTree-Spiegel des Live-Sets (Tracks/Clips/Scenes/
                        Devices, Key = stabile ID). Netzwerk-Thread →
                        MessageManager::callAsync → Tree. KEIN UndoManager —
                        Undo ist Lives Sache (/live/song/undo als Command).
  TouchLiveMeterBus        transiente Meter-/Playhead-Werte, NICHT im Tree
                        (Muster 4.6-Meter: UI liest pro Tick, nie cachen)
Source/UI/TouchLivePage/
  TouchLivePage            Ω-Page, Sub-Tab-Host (GRID/MIXER/DEVICE/BROWSER)
  GridView, MixerView   M1 — LiveSetModel-Listener, VBlank-Animation
  DeviceView, BrowserView   Platzhalter (M3/M4)
```

- Verbindungs-Lifecycle: connect → pro Domain `get` (Snapshot) → `subscribe`
  → Diffs anwenden. Reconnect = Snapshot neu (Tree-Diff, kein Flackern).
- Schreibweg: UI → Command senden → auf Domain-Diff warten (KEIN lokales
  Vorgreifen außer Fader-Drag: lokal folgen, eingehende Echos während des
  Drags unterdrücken — Echo-Suppression-Muster aus 7.3).
- Session-transiente IDs (stabile IDs der Gegenseite) werden NIE in Presets
  serialisiert (Regel CLAUDE.md 6).

## 4. OSC-API — M1-Auszug

**Commands (Conduit → Script), AbletonOSC-kompatibel:**

| Bereich | Adressen (Auswahl) |
|---|---|
| Transport | `start_playing` · `stop_playing` · `set/tempo` · `set/metronome` · `undo/redo` · `set/session_record` |
| Scene/Clip | `scene/fire` · `clip_slot/fire` · `clip/stop` · `track/stop_all_clips` |
| Mixer | `track/set/volume · panning · send · mute · solo · arm` · Master analog |
| View | `song/set/selected_track` · `selected_scene` |

**Domain-Snapshots (Script → Conduit), JSON-Payload pro Domain:**
`tracks` (Name, Farbe 0x00RRGGBB, Typ audio/midi/return/master, Reihenfolge) ·
`mixer` (Vol/Pan/Sends/Mute/Solo/Arm, Volume zusätzlich als dB-String) ·
`session` (Clip-Grid: pro Slot Zustand leer/gestoppt/spielt/queued/recording
+ Name/Farbe; Scene-Namen) · `transport` (Playing, Tempo, Taktposition,
Metronom, Session-Record). Detail-Schema wird beim Script-Bau festgezurrt.

## 5. UI — nah an Ableton, touch-optimiert

**GRID (Session-View):**

- Spalten = Tracks mit farbigem Track-Header (Live-Farbe), Zeilen = Scenes,
  rechte Spalte = Scene-Fire. Unterste Zeile: Clip-Stop pro Track.
- Clip-Zellen wie in Live: Farbe + Name, Play-Dreieck; Zustände gestoppt /
  **queued (blinkend)** / spielend / aufnehmend (rot) — Blink-Phase über
  VBlank, synchron zum Link-Beat.
- Scroll/Pan mit 1-Finger-Drag auf freier Fläche, Ring sichtbar verschiebbar;
  Zellgröße min. 44 px, Standard deutlich größer.

**MIXER:**

- Kanalzüge wie Lives Mixer, ein Zug pro Track + Master rechts.
- **Fader + Stereo-Meter nebeneinander** nach den beiden Referenzbildern:
  dB-Skala 0 / −12 / −24 / −36 / −48 / −60, Meter grün→gelb mit Peak-Hold,
  Fader-Cap als Dreiecks-Zeiger auf der Skala. Basis: `GainFaderMeter`
  aus dem FX-Chassis (4.6) — erweitern, nicht neu bauen.
- Darüber: Pan (horizontaler Mini-Slider), Sends (Mini-Fader oder Knobs,
  Anzahl dynamisch), darunter Mute/Solo/Arm als LED-Buttons (Push-Farben:
  gelb Mute-Off-Konvention wie Live, blau Solo, rot Arm), Track-Name auf
  Track-Farbe als Fußzeile.
- Fader-Drag: fein per Wisch-Distanz (relative mode), Doppeltipp = 0 dB.

**Allgemein:** PushLookAndFeel bleibt Basis (Kacheln, Jost, LED-Akzente),
Live-Track-Farben kommen als Akzent dazu. Alle Symbole als `juce::Path`
(PushIcons-Muster), keine Bitmaps.

### 5.1 Feel-Regeln (verbindlich — Anti-Ruckel, Referenz: Roto Control)

Die bekannten Schwächen von Grid/touchAble (ruckelige Fader) haben drei
Ursachen; jede bekommt eine feste Gegenmaßnahme:

1. **Kein Echo-Kampf:** Während ein Element berührt ist, folgt es NUR dem
   Finger (lokal-optimistisch). Eingehende Werte für diesen Parameter werden
   unterdrückt — Touch + 250 ms Nachlauf (Echo-Suppression-Muster 7.3).
   Das UI wartet NIE auf Live-Feedback, um sich zu bewegen.
2. **Hochraten-Touch-Pfad:** berührter Parameter sendet mit bis zu 60 Hz
   (thinned), alles andere normal. Kein Bundling des Touch-Werts mit
   Struktur-Traffic.
3. **Flüssiges Fremd-Feedback:** eingehende Werte (Automation, andere
   Controller) werden pro VBlank-Frame zum Zielwert interpoliert (kurzes
   Slew ~30 ms), nie hart gesetzt — Meter davon ausgenommen (roh).

Messlatte: Fader-Drag fühlt sich an wie ein lokaler Conduit-Fader.
Wenn nicht: erst Raten messen (Diagnose-Zähler im Client), dann schrauben.

## 6. Figma-Handoff (Bedienelemente, die du vorbereitest)

**Sinnvolle Komponenten-Liste:**

1. Kanalzug komplett (Fader-Cap, dB-Skala, Stereo-Meter, Pan, Send, M/S/Arm-Buttons)
2. Clip-Zelle: alle 5 Zustände + Scene-Button + Stop-Button
3. Track-Header (Name auf Track-Farbe, Typ-Icon audio/midi/return)
4. Transport-Ergänzungen fürs Remote-Tab (Session-Record, Re-Enable Automation)
5. Sub-Tab-Leiste GRID/MIXER/DEVICE/BROWSER

**Export-Regeln (damit es 1:1 in JUCE übernehmbar ist):**

- Flache **SVGs**, ein Pfad-Set pro Zustand, im **normierten Quadrat/Rechteck**
  (Koordinaten skalierbar 0..1 — PushIcons-Muster), keine Rasterbilder,
  keine SVG-Filter/Gradients in Icons (Meter-Gradient macht der Code).
- **Farben als Tokens benennen** (z. B. `led.green`, `track.colour`,
  `surface.tile`) — konkrete Werte liefert PushLookAndFeel bzw. die
  Live-Track-Farbe zur Laufzeit.
- Naming: `remote/{komponente}/{zustand}.svg`, Frame-Größen in 4-px-Raster,
  Touch-Targets ≥ 44 px einzeichnen.
- Die zwei gelieferten Referenzbilder (Fader-Skala, Meter-Verlauf) gelten
  als Design-Vorgabe für Skalenteilung und Meter-Farbverlauf.

## 6b. DEVICE-Strategie: generisch zuerst, bespoke danach

- **Generisches Panel (M3):** JEDES Device ist ab M3 steuerbar — Parameter
  als Fader-Spalten in Bänken à 8 (Layout-Verwandtschaft: FxModulePanel),
  On/Off, Bank-Navigation, Device-Kette als Chip-Leiste pro Track.
  Die Device-Domain liefert dafür `class_name`, Parameter-Namen, Ranges,
  `is_quantized` + Wertelisten (LOM DeviceParameter).
- **Bespoke Touch-UIs (M5):** Registry `class_name → Panel`; kein Treffer →
  generisches Panel. Reihenfolge (User-Entscheidung 07/2026):
  **EQ Eight** (Kurve mit Touch-Punkten) → **Compressor/Glue** (Kennlinie +
  GR-Meter, Threshold in der Anzeige ziehen) → **Delay/Reverb**. Danach nach
  Bedarf — Ziel bleibt: alle Stock-Devices fernsteuerbar (generisch),
  die wichtigsten touch-nativ.

## 6c. Modulator-Zwillinge (LFO & Co.) — der Fernmodulations-Trick

**Idee:** Lives Modulatoren (LFO, Shaper, Envelope Follower) werden als
Conduit-Generator-Module nachgebaut. Synchronisiert werden nur die
**Reglerpositionen** — nie der Modulationsstrom. Der Conduit-Zwilling läuft
nativ im Audio-Thread und moduliert über das DC-coupled Interface analoge
Hardware mit echter Audio-Rate — 10 m vom Ableton-Rechner entfernt.

- **Sync-Weg (User-Entscheidung 07/2026):** das Remote Script beobachtet
  Stock-Modulator-Devices (Erkennung via `class_name`) und synct deren
  Parameter bidirektional über die Device-Domain. Kein Extra-Device im Set,
  funktioniert mit jedem bestehenden Projekt. (Die M4L-Announce-Infrastruktur
  aus 7.4 bleibt der Weg für eigene Spezial-Devices — beides koexistiert.)
- **Phasenkohärenz gratis durch Link:** Im Beat-Sync-Modus teilen sich
  Ableton-LFO und Conduit-Zwilling das Link-Beat-Grid — beide sind
  phasengleich, ohne dass ein Modulationswert übertragen wird.
  Freilaufender Hz-Modus: driftet naturgemäß → periodischer Phase-Re-Anker
  (Script sendet Phasen-Marker ~1×/s, Zwilling korrigiert slew-limitiert,
  Muster Looper-Playhead).
- **Mapping-Tabellen** pro Modulator-Typ (Ableton-Param → Conduit-Param,
  inkl. Kurven/Ranges) sind Teil des Moduls, versioniert über
  `getStateVersion()`.
- Baut auf M3 (Device-Domain) auf; die Module selbst sind normale
  GeneratorModule (IClockSlave, CV-Out) — Spezifikation im Repo, wenn M6
  ansteht.

## 7. Meilensteine

| M | Inhalt |
|---|---|
| M1a | Script-Basis: AbletonOSC-Fork, Ports, Domains transport/tracks/mixer/session, Heartbeat, Touch-Pfad — **erledigt** (Tools/Live/ConduitRemote, 120 pytest-Tests) |
| M1b | Conduit: TouchLiveClient + LiveSetModel + TouchLiveSettings (IP-Learn), Snapshot/Diff/Reconnect — **erledigt 09.07.2026** (§10) |
| M1c | GRID- + MIXER-Sub-Tab (UI nach §5 inkl. Feel-Regeln 5.1) — **erledigt 09.07.2026** (§10b; User-SVGs Fader/Icon eingepflegt, weitere Figma-Assets folgen stückweise) |
| M2 | Meter-Pfad (TouchLiveMeterBus), Feinschliff Fader-Gesten, Feel-Abnahme gegen Roto-Messlatte — **KOMPLETT 09.07.2026**: Meter-Pfad (§10c) + Fast-Path v2 (§10e); Feel-Abnahme im Feldtest Runde 3 bestanden („working perfectly"). Offen nur noch LiveFaderScale-Feinkalibrierung (§11) |
| M3 | DEVICE generisch: Device-Domain, Ketten-Navigation, Parameter-Bänke, On/Off (§6b) — **erledigt 09.07.2026** (§10g; Feldtest offen) |
| M4 | BROWSER: Baum via `load_children`-Muster, Laden auf Track/Chain, Preview — **erledigt + Feldtest bestanden 10.07.2026** (§10h) |
| M5 | Bespoke Device-UIs (§6b): **EQ Eight erledigt + Smoke gegen Live verifiziert 10.07.2026** (§10i) — Compressor/Glue → Delay/Reverb offen |
| M6 | Modulator-Zwillinge: LFO zuerst, dann Shaper/Envelope Follower (§6c) |

## 8. Tests

- Script: pytest gegen Live-Stub (Handler-Registrierung, Diff-Erzeugung,
  Payload-Grenze, stabile IDs über Reorder).
- Conduit: Catch2 — LiveSetModel-Snapshot/Diff-Anwendung, Reconnect ohne
  Tree-Neuaufbau, Echo-Suppression beim Fader-Drag (`IOscSink`-Seam aus 7.3).

## 9. Integration ins Repo (Dossier-Konventionen, CLAUDE.md v5.0 §1.1)

- Dieses Dokument wird zum Subsystem-Dossier **`docs/TouchLive.md`**.
- Invarianten als path-scoped Rule **`.claude/rules/touchlive.md`**
  (`paths:`-Frontmatter: `Source/TouchLive/**`, `Tests/TouchLive/**` —
  NIE `globs:`, ADR 005).
- CLAUDE.md: nur Roadmap-Zeile + Verweis aufs Dossier (keine Detail-Specs).
- Neue Ordner: `Source/TouchLive/`, später `Source/UI/TouchLivePage/`,
  `Tools/Live/ConduitRemote/`.
- PageHost: eigene TouchLive-Page ergänzen (M1c; Icon/Slot offen — Ω ist vom
  Grid-Touch-Controller belegt).

## 10. M1b — Conduit-Client: Implementierungs-Notizen (09.07.2026)

Umgesetzt in `Source/TouchLive/` (TouchLiveSettings · IRemoteTransport ·
LiveSetModel · TouchLiveClient), Catch2-Tests in `Tests/TouchLive/`.
Noch NICHT im EngineProcessor verdrahtet — das macht M1c mit der Page.

- **Threading-Umsetzung:** statt rohem `MessageManager::callAsync` pro
  Message queued der Netzwerk-Thread in einen gelockten Vector +
  `AsyncUpdater` (Muster OscController) — gleiche Message-Thread-Garantie,
  aber synchron testbar (`flushPendingMessages()`). JSON-Parsing passiert
  erst auf dem Message Thread (Remote ist nie im Audio-Pfad, String-Ops ok).
- **var-Falle (Flacker-Schutz):** `juce::var` vergleicht DynamicObjects nur
  über Pointer, Arrays elementweise via `var::operator==` — frisch geparstes
  JSON wäre für `ValueTree::setProperty` also IMMER „anders" (v. a. die
  Session-Grid-Zeilen mit Clip-Objekten). LiveSetModel vergleicht deshalb
  deep VOR jedem setProperty; der Reconnect-Listener-Test beweist 0 Events
  bei identischem Snapshot. (Test-Gotcha dazu: ValueTree-Listener hängen an
  der ValueTree-INSTANZ — `getState().addListener(...)` auf ein Temporary
  ist ein No-op.)
- **Modell-Form:** Objekt-Werte → `Item`-Kinder (`itemKey` = Stable-ID,
  JSON-Felder als Properties), Skalar-/Array-Werte → Properties direkt am
  `Domain`-Tree (Transport-Felder, `grid:{tid}`-Zeilen). Identifier
  `domainName`/`itemKey` bewusst kollisionsfrei zu JSON-Feldnamen. Ein
  Diff-Objektwert ersetzt sein Item KOMPLETT (compute_diff der Gegenseite
  ist shallow) — fehlende Felder werden entfernt.
- **Seq/Chunks:** Snapshot akzeptiert jede seq und setzt den Zähler neu;
  Diff nur bei exakt `lastSeq + 1` (älter → Duplikat, Lücke → `/get`).
  Re-Requests sind pro Domain gedrosselt (ein `/get` pro Heartbeat-
  Intervall) — ein verlorener `/get` heilt so nach ≤ 2 s. Chunks gleicher
  seq werden gesammelt (Reihenfolge egal) und als EIN Payload angewendet;
  Kind-/Seq-Wechsel verwirft eine unfertige Sammlung.
- **Heartbeat/Reconnect:** Ping-Kadenz (2 s) IST der Reconnect-Backoff
  (UDP). JEDER Übergang zu connected subscribed alle Domains neu — damit
  heilen auch beim Enable verlorene Subscribes (Script noch nicht
  gestartet); der doppelte Start-Snapshot ist gewollt billig, weil er als
  Tree-Diff im Modell landet. Status disabled/connecting/connected/
  disconnected via ChangeBroadcaster (M1c-Statusanzeige).
- **Echo-Suppression/Thinning:** Suppression-Key = `domain/stableId/feld`
  (`makeParameterKey`), Ablauf 250 ms nach letztem `noteTouchedParameter`;
  das Prädikat reicht bis in die Feld-Anwendung des Modells (unterdrückte
  Felder werden weder gesetzt noch entfernt). `sendTouchValue` drosselt pro
  Adresse auf ~16 ms, letzter Wert gewinnt, Nachzügler flusht ein Timer.
  Zeitquelle injizierbar (`setTimeSource`) → deterministische Tests.
- **IP-Learn:** das Script sendet NIE spontan (antwortet nur an Absender-
  IP:9011) — die Learn-Probe (Muster docs/OscSend.md) broadcastet deshalb
  selbst periodisch ein handkodiertes `/remote/ping` (20-Byte-OSC) an
  255.255.255.255:9010 + konfigurierten Host und liest die Absender-IP des
  Pongs vom freigegebenen Listen-Port.
- **Codec-Befund Gegenseite:** `osc/codec.py` encodiert Python-Bools als
  OSC-T/F-Typetags — juce_osc unterstützt T/F NICHT. Richtung
  Script→Conduit geht nur `[i,i,i,s]` bzw. Pong `[i]` über den Draht
  (Bools reisen im JSON) — beim Erweitern der Gegenseite nie nackte Bools
  an Conduit senden.

## 10b. M1c — Page-UI: Implementierungs-Notizen (09.07.2026)

Umgesetzt in `Source/UI/TouchLivePage/` (TouchLivePage · TouchLiveGridView ·
TouchLiveMixerView/ChannelStrip · TouchLiveFader) + `TouchLive/LiveFaderScale.h`;
verdrahtet im EngineProcessor (Settings/Modell/Client als Member) und
EngineEditor (Page vor dem PageHost). Tests: Tests/UI/TouchLivePageTests.cpp,
Tests/TouchLive/LiveFaderScaleTests.cpp.

- **Slot-Entscheidung (User 09.07.2026):** die TouchLive-Page ersetzt
  vorerst die Clip-Page (PageIndex 2, `pageTouchLive`); Icon = User-SVG
  (drei Mini-Kanalzüge, `TouchLive.svg`, als juce::Path portiert —
  PushIcons-Muster, Fill-only). Die Clip-Page bekommt später wieder einen
  eigenen Slot.
- **Fader in Conduit-Push-Optik** (User-Entscheidung 09.07.2026 abends:
  die eigenen Fader-Skizzen sind vorerst GEPARKT — „bau die Seite so, wie
  sie zu Conduit passt"; Idee für später: zwei Skins „Conduit / Ableton
  mirrored", §11): Rinne + Füllung von unten + Griffstein mit Mittellinie
  (Formsprache PushLookAndFeel::drawLinearSlider), dB-Labels + Ticks
  rechts (0/12/24/36/48/60). Positionsskala ist dB-LINEAR (+6…−72, wie
  die FX-Chassis-Gain-Fader). Label-Dichte halbiert sich automatisch
  unter ~16 px Tick-Abstand (User: „je nach Skalierung"). Stereo-Meter
  = eigene Spalte mit M2 (GainFaderMeter-Muster). Das Page-Icon bleibt
  das User-SVG.
- **LiveFaderScale:** Wert↔dB-Näherung (0.85→0 dB, 1.0→+6 dB; unter 0.4
  log-Auslauf). Community-Fit, unter −18 dB ungenau → nach dem Feldtest
  gegen Lives Anzeige kalibrieren (§11).
- **Feldnamen-Falle:** die mixer-Domain liefert `vol`/`pan` (NICHT
  volume/panning) + `sends`-Array + `mute`/`solo`/`arm`; der Master lässt
  sends/mute/solo/arm komplett weg — die UI blendet Regler nach
  Feld-Existenz/Kind aus.
- **Adressierungs-Grenze der Gegenseite:** `_resolve_stable_id` iteriert
  NUR `song.tracks` — Returns/Master sind per track_ref nicht erreichbar.
  Returns senden über `/live/return/set/volume|panning` (Index), Master
  über `/live/master/set/*`; Return-Sends/Mute/Solo bleiben in der UI
  ausgeblendet, bis das Script sie kann (M2-Kandidat).
- **MixerView:** Strips werden bei Struktur-Änderungen coalesced
  (AsyncUpdater) neu aufgebaut, aber per Stable-ID WIEDERVERWENDET (kein
  Flackern, Drag-Zustand überlebt); reine Wert-Diffs gehen direkt an den
  Strip (Fader-Slew 30 ms via AnimatedValue — headless snappt er, Tests).
  Buttons schalten optimistisch + Suppression-Note (Command-Pfad läuft
  über Lives ~100-ms-Tick).
- **GridView:** Zellen paint-only (kein Component pro Zelle), Header/
  Scene-Spalte/Stop-Zeile angepinnt, manueller Scroll (Tap vs. Pan über
  8-px-Schwelle — Gesten-Kernpfad `tapAt()` testbar). Queued-Blink über
  VBlank ~2 Hz zeitbasiert; Link-Beat-Kopplung = M2-Feinschliff.
- **Kanalbreite** (`TouchLiveSettings::channelWidth`, 56–200 px, Header-
  Kachel KANAL): steuert Mixer-Zugbreite UND Grid-Spaltenbreite — das ist
  die User-Einstellung „wie viele Tracks parallel".
- **EngineProcessor-Hinweis:** Settings lesen die echte
  `Conduit/TouchLive.settings` — auch in Tests, die einen EngineProcessor
  bauen. Enabled default aus ⇒ keine Sockets; auf Dev-Maschinen mit
  aktivierter Remote öffnen solche Tests den Listen-Port (harmlos,
  Bind-Fehler wird 2-s-weise erneut versucht).

## 10c. M2 — Meter-Pfad: Implementierungs-Notizen (09.07.2026)

Beidseitig umgesetzt: `sync/meters.py` (Script) + `TouchLiveMeterBus` /
Fader-Meter-Spalte (Conduit). pytest neu: test_meters.py; Catch2 neu:
Meter-Parsing/Bus/Ballistik.

- **Wire-Format (bewusst KEIN Domain-Diff):** `/remote/meters` mit flachen
  Tripeln `[id:str, left:float, right:float] × n` (Tracks + Returns +
  Master, gleiche Stable-IDs wie tracks/mixer). KEINE seq — Frames sind
  idempotent, ein verlorenes/spätes Datagramm ist bei ~10 Hz unsichtbar,
  der nächste Frame überschreibt alles. Kein JSON (Parse-Kosten pro Tick).
- **Stille-Dedupe:** ein All-Zero-Frame geht noch raus (Client-Meter
  fallen auf Ruhe), danach schweigt der Stream bis wieder Pegel kommt —
  idle kostet keine Bandbreite. Subscription `/remote/meters/subscribe`
  (Client schickt sie in subscribeAll mit), Heartbeat-Timeout beendet.
- **Werte:** Lives rohe `output_meter_left/right`-Norm (0..1, Lives eigene
  Ballistik). Anzeige 1:1 als Balkenhöhe; dB-genaue Kalibrierung gegen
  Lives Meter = Feldtest-Punkt (§11).
- **Conduit-Seite:** MeterBus = Message-Thread-Map + Frame-Zähler (NIE im
  Tree, UI liest pro Tick); `clear()` zählt ebenfalls hoch (Test-Falle:
  Baseline nehmen). MixerView pollt @ 30 Hz nur bei sichtbarer Page;
  UI-Ballistik im Fader: Anstieg sofort, Balken-Abfall weich
  (Faktor 0.72/Tick), Peak-Hold sinkt langsam — so wirken 10-Hz-Frames
  nicht stufig. Meter sind ROH: kein Slew, keine Echo-Suppression (§5.1).
- **Diagnose-Zähler** (`TouchLiveClient::getStats()`): Snapshots/Diffs/
  Meter-Frames/Touch-Sends kumulativ seit Enable — die Messbasis für die
  Feel-Abnahme („erst Raten messen, dann schrauben").
- **CI:** neuer Job `remote-script` (pytest auf Ubuntu) — lokal kollidieren
  die Manager-Tests mit laufendem Live (Port 9010 belegt), in der CI
  laufen immer alle. Python lokal: winget Python 3.12 (09.07.2026).

## 10d. Feldtest-Befunde 09.07.2026 (M2) — echte LOM-Fallen

Der erste Meter-/Fader-Feldtest deckte drei Live-API-Eigenschaften auf,
die der Test-Stub zu gutmütig simuliert hatte (Stub seither realistisch —
diese Fallen sind jetzt dauerhaft testabgedeckt):

1. **LOM-Wrapper sind nicht identitätsstabil:** jeder `song.tracks`-Zugriff
   kann neue Python-Wrapper liefern — `id(obj)` zerfällt, die Stable-IDs
   wurden pro Tick neu vergeben. Symptome: track_ref-Resolver fand nichts
   (Track-Volume-Commands tot), Domain-Keys passten nicht zu den Strips
   (keine Fader-Rückrichtung, keine Meter-Zuordnung), Dauer-Diffs mit
   frischen Keys (Strip-Rebuild pro Tick = Ruckeln). Fix: `_live_ptr` als
   Identitätsschlüssel (sessionstabil), `id()` nur als Stub-Fallback.
2. **`track.arm` WIRFT auf nicht armbaren Tracks** (Returns/Master),
   `mute`/`solo` wirft auf dem Master — Zugriff UND Listener-Binding.
   `_full_state()` starb daran bei jedem collect() → Mixer-Domain flog
   nach dem 5-Fehler-Budget raus (keine Mixer-Daten). Fix: Fähigkeits-
   Guards (`can_be_armed`, try/except pro Property), fehlende Fähigkeit
   = Key fehlt (Client blendet Regler aus, tat er schon).
3. **`output_meter_*` kann werfen** (Tracks ohne Audio-Ausgang) —
   Meter-Reads defensiv (0/0 statt Stream-Abbruch).

Erwartung nach den Fixes: Track-Volume bidirektional, Mixer-Rückrichtung
für alle Züge, Meter sichtbar, Ruckeln weg (kein Key-Churn mehr). Feel
danach neu bewerten (getStats-Raten), dann LiveFaderScale kalibrieren.
→ Runde 2 bestätigte Bidirektionalität (nach IP-LEARN); Stufigkeit blieb
→ §10e.

## 10e. Feldtest-Runde 2 (09.07.2026 abends) — GIL-Befund & Fast-Path v2

- **Messung (User):** Volume-Rampe als Audio aufgenommen (Operator Pre FX
  vs. Post Mixer) — die Hüllkurve steigt in **111-ms-Stufen** = Lives
  Scheduler-Tick. Log zeigt KEINE Fast-Handler-Fehler → der Empfangs-
  Thread lief, wurde aber von Lives embedded Python nur im ~100-ms-Tick
  gescheduled (GIL bleibt beim Host). **Ein RX-Thread bringt in Live
  NICHTS** — exakt deshalb sind touchAble/Grip stufig; der PC des Users
  war unschuldig.
- **Fast-Path v2:** RX-Thread ersatzlos raus; `OscServer.pump()` wird von
  einem **`Live.Base.Timer`** (~10 ms, C++-seitig, Callbacks auf dem MAIN
  Thread — AbletonJS/ClyphX-Pro-Muster) getrieben: Whitelist-Writes werden
  sofort UND LOM-sicher angewendet, Rest in die Queue für den Tick. Ohne
  Timer (ältere Live?/Tests) → `pump_active` False, process() liest selbst
  (Tick-Raten-Fallback). Test-Seams: `timer_factory`-Injection (Manager),
  FakeTimer.fire().
- **Zweiter Log-Befund:** `add_scenes_listener` wirft in Live 12.4.5b3
  (Boost ArgumentError) → Session-Domain starb beim Subscribe (Grid leer).
  Generischer **Poll-Fallback in Domain.attach()**: wirft on_attach, läuft
  die Domain listener-los weiter (collect+diff pro Tick — compute_diff +
  Sender-Dedupe halten das billig). Dazu Teardown-/Rebind-Guards überall
  (tote LOM-Objekte werfen beim remove_*_listener).
- Meter-Kadenz: seit dem Kalibrier-Feldtest am Timer-Pfad (~33 Hz,
  `METER_PUMP_DIVIDER` — User-Feedback: „Meter so flüssig wie die Fader");
  ohne Timer Fallback auf den Tick (~10 Hz). Der Timer-Block im Manager
  steht bewusst am ENDE des __init__ (kann sofort feuern, braucht meters).
- **Feldtest-Runde 3 (User-Abnahme):** „It's working perfectly now" —
  Fader-Feel-Messlatte (Roto, §5.1) BESTANDEN; die Stufigkeit ist mit
  Fast-Path v2 weg. Damit ist M2 komplett.

## 10f. LiveFaderScale-Kalibrierung: VERIFIZIERT (09.07.2026)

Messaufbau (User): Operator-Referenzton, Conduit-Fader nacheinander auf
die dB-Readouts −60 … +6 gestellt, Lives Post-Mixer-Signal als 32-bit-WAV
aufgenommen; Plateau-Peaks per Skript vermessen (Quellreferenz −0.046 dBFS
= Lives Peak-Anzeige −0.05). Ergebnis: **alle neun Stützstellen treffen
auf < 0.05 dB** — auch der Log-Auslauf unter −18 dB (−24/−36/−48/−60
exakt). Die „Näherung" ist damit Lives tatsächliche Kurve; keine
Code-Änderung nötig, §11-Punkt geschlossen. (Einziger Messausreißer:
−12-Plateau bei −11.9 = Einstellgenauigkeit des Drags, std 0.05 zeigt
Nachjustieren.)

## 10g. M3 — DEVICE generisch: Implementierungs-Notizen (09.07.2026)

Beidseitig: `sync/devices.py` + `handlers/device.py` (Script) und
`TouchLiveDeviceView` (Conduit, ersetzt den DEVICE-Platzhalter).

- **Wire-Form (Shallow-Diff-Granularität, Session-Grid-Lektion):**
  `chain:{tid}` = Ketten-Array pro Track (Tracks + Returns + Master) ·
  `dev:{dvid}` = Struktur-Item (name/class_name/is_active) ·
  `parmeta:{dvid}` = statische Parameter-Metadaten (name/min/max/quant,
  quantisierte mit `items`-Werteliste) · `parvals:{dvid}` = HEISSE Zeile
  (nur Floats) — eine Parameteränderung difft ausschließlich das kleine
  Werte-Array, nie die Metadaten. Stable-ID-Präfix `dv:`.
- **Scope-Grenze M3:** nur TOP-LEVEL-Devices — keine Rack-/Chain-Rekursion
  (kommt mit den bespoke-UIs, §6b). `parameters[0]` ist Lives „Device On"
  und reist mit; die Bänke der UI beginnen bei Index 1, der Schalter läuft
  über `/live/device/set/is_active` (dev:-Item).
- **Commands:** `/live/device/set/parameter [dvid, index, value]` (auf der
  FAST_WHITELIST → Timer-Pump, fühlt sich wie die Fader an) und
  `/live/device/set/is_active [dvid, 0|1]`. Resolver `_resolve_device`
  sucht über alle Ketten-Träger (gleiche `_live_ptr`-Registry wie die
  Domain).
- **Conduit-View:** Track-Chips → Device-Chips (mit On-LED) → 8er-Bank
  (Name · Slider · Wertetext; quantisierte zeigen `items`), ‹ ›-Bank-
  Navigation + ON-Tile. Struktur-Rebuild coalesced; `parvals:`-Diffs des
  gewählten Devices gehen direkt in die Slider (kein Rebuild). Auswahl =
  Laufzeit-Zustand (fällt bei verschwundener Kette aufs erste Gerät des
  ersten bestückten Tracks zurück), Suppression-Keys:
  `devices/parvals:{dvid}` (Slider) bzw. `devices/dev:{dvid}/is_active`.
- **Gain Reduction (User-Wunsch, Push-Vorbild):** Devices mit
  `gain_reduction` hängen ihr Tripel `[dvid, gr, gr]` an den
  `/remote/meters`-Frame; die DeviceView zeigt die GR-Spalte rechts der
  Bank (füllt von OBEN), sobald je ein Wert kam. **FELDTEST-BEFUND
  10.07.2026: In Live 12.4.5b3 exponiert das Python-LOM die Compressor-GR
  NICHT** — per Einmal-Diagnose bewiesen (`gain_reduction on 0/4 devices`,
  dir()-Scan des Compressor2 ohne gain/reduc-Attribute; auch kein
  versteckter DeviceParameter, alle 3 Bänke geprüft). Push bezieht seine
  GR-Kurve über den proprietären Display-Datenkanal der Push-Integration,
  nicht über Python. Der Sende-/Anzeige-Code bleibt drin (kostenlos,
  greift automatisch, falls Ableton die Property später freigibt oder ein
  Device sie liefert). **Gangbarer Weg, wenn gewünscht:** Mini-M4L-Device
  („GR-Tap", Envelope-Differenz Pre/Post) hinter dem Compressor — dessen
  Parameter reisen automatisch durch die devices-Domain; passt zur
  M4L-Announce-Schiene (7.4). Die Einmal-Diagnose bleibt im MeterStream
  (loggt pro Subscribe eine INFO-Zeile ins Live-Log).
- **Offen für den Feldtest:** LOM-Fallen der Device-Parameter in 12.4b
  (alle Zugriffe sind geguarded + Poll-Fallback), Payload-Größe bei sehr
  großen Racks (Chunking vorhanden), Anzeige-Einheiten (parmeta trägt
  keine Display-Strings — Lives `str_for_value` wäre ein M5-Kandidat),
  GR-Wertebereich/Skalierung.

## 10h. M4 — Browser: Implementierungs-Notizen (10.07.2026)

Beidseitig: `browser.py` (Script, BrowserService) + `TouchLiveBrowserView`
(Conduit, ersetzt den letzten Platzhalter — alle vier Sub-Tabs leben).

- **Bewusst KEINE Domain:** Lives Browser-Baum ist riesig und lazy —
  reines Request/Response (touchAble-`load_children`-Muster):
  `/remote/browser/roots` bzw. `/children [parent_id]` →
  `/remote/browser/list [seq,chunk,chunks,json]` mit
  `{"p": parent, "it": [[id,name,folder,loadable],…]}`. Antworten laufen
  über `Sender.send_json_list` (force=True): Chunking auf **Listen-
  Element-Ebene** — jeder Chunk trägt `p` plus eine Scheibe des
  `it`-Arrays, der Client konkateniert die Scheiben einer seq.
  Verlorene Antworten heilt der nächste Tap (kein Seq-Healing nötig).
- **Node-IDs** vergibt das Script pro Session (Registry hält die
  BrowserItems am Leben, wie stable_ids) — Laufzeit-IDs, NIE
  persistieren. Wurzeln = feste Attributliste (sounds…current_project),
  fehlende werden übersprungen (LOM-defensiv wie überall).
- **Laden:** `browser.load_item` zielt auf Lives Track-Selektion —
  dafür neu: `/live/song/set/selected_track [track_ref]`
  (Song.View). Preview: `preview_item`/`stop_preview`.
- **Conduit-View:** Breadcrumb + 44-px-Zeilenliste (paint-only im
  Viewport), Ebenen-Cache (Zurück ohne Re-Request), Tap=Ordner öffnen/
  Item wählen, Doppeltipp oder LOAD-Kachel = laden, PRE-Kachel =
  Vorhör-Modus (Tap spielt an, Ausschalten stoppt). Roots werden beim
  ersten Sichtbarwerden angefordert. `onBrowserList`-Callback am Client
  wird von der View exklusiv belegt.
- **Feldtest bestanden (10.07.2026)** — Roots → Drums → Drum Hits →
  Kick → PRE-Vorhören → LOAD (Sample landet als Simpler auf Lives
  gewähltem Track). Drei Befunde, alle gefixt:
  1. **Lives Kategorie-Wurzeln (Sounds…Packs) melden `is_folder=False`**,
     haben aber Kinder — `tapRow` öffnet deshalb ALLES, was nicht ladbar
     ist (nicht nur echte Ordner); Chevron-Marker analog.
  2. **Key-Level-Chunking verwarf große Ordner komplett:** die Drums-
     Liste ist EIN `it`-Key > max_bytes → „dropping oversized key", beim
     Client kam eine leere Ebene an. Fix: `Sender.send_json_list`
     (elementweises Listen-Chunking, exakte Größenmathematik über
     kompaktes ASCII-JSON, Antwort geht IMMER raus — notfalls leer).
  3. **Live-Neustart ließ den Browser auf toter Ebene hängen** (Session-
     transiente Node-IDs der alten Script-Session): Client-Callback
     `onReconnected` (feuert bei jedem Übergang zu connected, neben
     `subscribeAll`) — die View verwirft alle Ebenen und fordert
     sichtbar die Wurzeln frisch an.
- **Feldtest offen:** Performance sehr großer Packs (children ist im
  Tick synchron — bei Riesenordnern spürbar?), Laden-auf-Chain
  (M5-Umfeld).

## 10i. M5 — Bespoke EQ Eight: Implementierungs-Notizen (10.07.2026)

Conduit-seitig, KEINE Script-Änderung (die devices-Domain aus M3 reicht):
`TouchLiveBespokePanel.h` (Interface + Registry) · `TouchLiveEq8Panel` ·
DeviceView-Integration.

- **Registry (§6b):** `createBespokePanel (class_name, client)` → Panel
  oder nullptr; die DeviceView zeigt das Panel statt der Bank NUR wenn
  es zusätzlich `isUsable()` meldet (Parameter-Mapping gelungen) —
  sonst Bank-Fallback, jedes Device bleibt IMMER steuerbar. viewTile
  (BANK ↔ Kürzel) schaltet jederzeit um; die Bank behält alle
  Rest-Parameter (Scale, Adaptive Q, Output …).
- **Parameter-Mapping über Namen der A-Kurve**, nie Indizes:
  `"{n} Filter On A"` · `"{n} Filter Type A"` (items!) ·
  `"{n} Frequency A"` · `"{n} Gain A"` · **`"{n} Q A"` — Live 12 nennt
  den Q-Parameter „Q A", NICHT „Resonance A"** (Feldtest-Befund;
  „Resonance A" bleibt als Alias). Typ-SEMANTIK aus den items-Strings
  (lowcut/shelf/bell/notch…), Fallback Live-12-Reihenfolge.
- **Wertesemantik im Smoke gegen Live VERIFIZIERT** (Kreuzcheck
  Band 3: Conduit 1.58 kHz/+6.3 dB/Q 0.71 == Lives Anzeige
  1.58 kHz/6.31 dB/0.71): Frequency-Norm → Hz = 10·2200^v (log
  10 Hz–22 kHz), Q-Norm → Q = 0.1·180^v (log 0.1–18), Gain direkt dB
  (parmeta min/max ±15).
- **UI:** RBJ-Biquad-Summenkurve als Display-Näherung (48er-Cuts =
  vierfach kaskadierte 12er), 8 Touch-Punkte (Drag: X=Frequenz,
  Y=Gain nur bei Bell/Shelf; Doppeltipp = Band an/aus; Trefferzone
  26 px), Footer: Typ-Stepper ‹ › · Q-Slider · Band-ON; Readout
  oben rechts. Punkte folgen §5.1 lokal-optimistisch (setValues lässt
  das gezogene Band in Ruhe), Suppression-Key wie die Bank.
- **Thinning-Fix im Client (drüber gestolpert):** der Drosselkanal
  von sendTouchValue war die OSC-ADRESSE — Frequenz + Gain
  (/live/device/set/parameter) latchten sich gegenseitig weg, genauso
  Multi-Touch auf zwei Fadern (/live/track/set/volume). Kanal ist
  jetzt Adresse + Argumente OHNE den Wert (`touchKeyFor`).
- **Offen (M5-Folgerunden):** Compressor/Glue (Kennlinie + GR),
  Delay/Reverb; B-Kurve/Edit-Mode, Scale-Einrechnung in die
  Anzeige-Kurve, Rack-Chains (§10g-Scope-Grenze gilt weiter).

## 10j. M5-Politur — EQ-Kurven-Kalibrierung + Ableton-Look (10.07.2026)

User-Messkampagne: 70 Side-by-Side-Screenshots (Conduit + Live, Band 2
@ 200 Hz, Gain/Q/Typ im Device-Panel ablesbar) → Python-Pipeline im
Scratchpad (eq8_extract/analyse/fit): Kurven-Pixel per Live-Farbfilter
(Lives Kurve g/b≈0.93 vs. Conduits ledCyan 0.88!), dB-Skala aus
Gridlinien + Rechtsrand-Anker, 200-Hz-Anker über den Band-Handle,
px/Dekade via Log-Raster-Fit. Ergebnis: **Lives Anzeige-Kurven sind
analoge RBJ-Prototypen mit eigener Q-Semantik** — kalibriert auf
< 0.4 dB über alle 60+ Messkurven:

| Typ | Prototyp (s-Domain) | Q_eff (Adaptive Q On; g = \|gain dB\|) |
|---|---|---|
| Bell | RBJ Peaking | `Q · 0.5151 · 10^(0.04908·g)` |
| Shelf (Low/High) | RBJ Shelf | `10^(−0.36661 + 0.45166·lgQ + 0.04382·g − 0.00685·lgQ·g)` |
| Cut 12 | RBJ HP/LP | `Q` (1:1, kein Adaptive Q) |
| Cut 48 | 4× Butterworth-8-Kaskade (Qs 0.51/0.60/0.90/2.56) | alle Stufen × `(1.097 + 0.611·lgQ)` |
| Notch | RBJ Notch | `Q` (1:1) |

- Der „Adaptive Q"-Parameter reist in parvals und schaltet den
  Gain-Term (Off-Verhalten = Annahme, nicht gemessen — bei Bedarf
  nachmessen). High-Pendants = gespiegelte Gesetze (Annahme).
- **Anzeige wie Live:** ±15 dB Y-Range (Linien ±12/±6/0 mit Zahlen),
  Frequenzraster mit Dekaden-Zwischenlinien + 100/1k/10k-Labels,
  Kurve in Lives Cyan (#03cfde) 2 px, Auflösung an Pixelbreite
  gekoppelt (Spitzen wurden mit fixen 96 Stützstellen eckig);
  Handles im Ableton-Look: orange Nummernkreise Ø 44 px
  (Auswahl gefüllt Ø 54, dunkle Zahl; aus = grau) — User-Wunsch
  „größer als ein Zeigefinger".
- **Multi-Touch-Gesten (User-Spezifikation 10.07.2026, Kernpfade
  touchDown/Move/Up als Zustandsmaschine):**
  1. Punkt HALTEN + weitere Punkte antippen = Mehrfachauswahl
     (selektierte Punkte gefüllt, aktiver größer);
  2. Punkt halten + freier zweiter Finger = Pinch → Q des aktiven
     Bandes (Abstand verdoppeln ≈ +0.25 Q-Norm ≈ Faktor 3.7; Freq/
     Gain-Drag friert währenddessen ein) — Q-Geste NUR bei
     berührtem Punkt;
  3. OHNE Punktberührung: 2 Finger = alle angewählten Bänder
     gemeinsam verschieben (Zentroid-Delta auf Freq/Gain, Snapshot
     beim Gesten-Start) · 3 Finger = Output-Gain fein · 4 Finger =
     Scale (je vertikal, 200 px ≈ 10 % der Range, großes Readout);
     Parameter „Output"/„Scale" per Name aus parmeta.
  4. Lässt der haltende Finger los, sind Restfinger bis zum Abheben
     wirkungslos (idle) — keine Gesten-Überraschungen.
  5. **Typ-Selector (User-Wunsch 10.07.2026):** Punkt berühren und
     ~1 s STILL halten → vertikale Symbolliste (Mini-Frequenzgänge
     aus derselben Filtermathematik, Lives Dropdown-Optik) öffnet am
     Punkt; hoch/runter wischen wählt, Loslassen übernimmt. Der
     Q-Fader im Footer ist entfallen (Pinch reicht). Voraussetzung
     dafür: **Band-Drag ist RELATIV mit 9-px-Schwelle** — der Punkt
     springt nie zur Fingerposition (Kernpfade dragActiveBandBy,
     triggerLongPress).
  6. **Cut/Notch-Punkte steuern per Y-Drag den Q** (Live-Verhalten,
     Feedback-Runde 2) — hoch = schärfer; Bell/Shelf behalten Y=Gain.
     Der Panel-Footer ist KOMPLETT entfallen (Band-ON = Doppeltipp,
     Typ = Long-Press); der Typ-Name steht im Readout oben rechts.
     Scale-Trim gröber als Output (0.0015/px vs. 0.0005/px).
- **Scale in der Kurve (Messkampagne 2, 11 Screenshots −200…+190 %):
  Scale skaliert die BAND-GAINS** (nur gainfähige Typen), geclampt
  auf ±15 dB, Cuts/Notch unberührt, negative Werte invertieren; die
  Q_eff-Gesetze wirken auf den skalierten Gain. Belege: Cut-Anteile
  identisch über alle Scales (ratio-p90 = 1.0), Sättigung großer
  Scales am Clamp, Lives Scale-Range ist −2..+2. Die Band-PUNKTE
  bleiben beim nominellen Gain (wie Live).
- Feldtest-Kreuzcheck: Band 2 auf +12.4 dB gezogen — Lives Display
  zeigt identische Werte UND identische Kurvenform.
- **Nächster Politur-Schritt (User-Wunsch):** Spektrum-Hintergrund via
  Link Audio (Signal des Tracks heimlich empfangen → FFT hinter der
  Kurve, eigener Average-Parameter LOKAL, nicht an Lives Regler
  gebunden) — eigene Runde, Audio-Thread + docs/LinkAudio.md-Pflicht.

## 11. Offen

- Feldtest KOMPLETT bestanden (09.07.2026): Bidirektionalität, Feel
  (Fast-Path v2) und LiveFaderScale (§10f, < 0.05 dB) — nichts mehr offen
  aus M1/M2 außer der Meter-Kadenz-Anhebung auf den Timer-Pfad.
- Bedienelement-Optik: User-Skizzen/SVGs geparkt (09.07.2026) — die Page
  läuft in Conduit-Push-Optik weiter; evtl. braucht es gar keine eigenen
  Assets mehr. Kandidat stattdessen: **zwei Skins** („Conduit" ·
  „Ableton mirrored") als umschaltbares Theme — entscheiden, wenn M2
  steht. Das Page-Icon (User-SVG) bleibt.
- Meter-Raten-Budget final messen (UDP-Last bei 16+ Tracks).
- DEVICE/BROWSER-API-Detailschema (bei M3/M4 festzurren).
- Mindest-Live-Version: 11 oder 12 (Script-API-Unterschiede prüfen;
  Modulator-`class_name`s der Stock-Devices je Version verifizieren).
- Phase-Re-Anker-Format für freilaufende Modulatoren (§6c, bei M6).
