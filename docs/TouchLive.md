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
| M2 | Meter-Pfad (TouchLiveMeterBus), Feinschliff Fader-Gesten, Feel-Abnahme gegen Roto-Messlatte |
| M3 | DEVICE generisch: Device-Domain, Ketten-Navigation, Parameter-Bänke, On/Off (§6b) |
| M4 | BROWSER: Baum via `load_children`-Muster, Laden auf Track/Chain, Preview |
| M5 | Bespoke Device-UIs: EQ Eight → Compressor/Glue → Delay/Reverb (§6b) |
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

## 11. Offen

- Feldtest-Erstkontakt BESTANDEN (09.07.2026): Verbindung + Domain-Sync
  laufen, Namen/Farben erscheinen (User-Abnahme). Offen bleibt die
  Feel-Abnahme gegen die Roto-Messlatte (§5.1) und die Kalibrierung von
  `LiveFaderScale` unter −18 dB gegen Lives dB-Anzeige.
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
