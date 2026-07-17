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

Devices (M3):
- Wire-Form flach: `chain:{tid}` · `dev:{dvid}` · `parmeta:{dvid}` ·
  `parvals:{dvid}` (heiße Zeile — Werte-Diffs berühren NIE die Metadaten).
  Nur Top-Level-Devices (keine Rack-Rekursion bis M5). parameters[0] =
  „Device On" reist mit; UI-Bänke starten bei Index 1, Schalter über
  `/live/device/set/is_active`. `/live/device/set/parameter` gehört auf
  die FAST_WHITELIST.

Bespoke-UIs (M5):
- Registry `createBespokePanel (class_name)` (TouchLiveBespokePanel.h);
  Panel ersetzt die Bank NUR wenn `isUsable()` (Mapping gelungen) —
  sonst Bank-Fallback; viewTile schaltet jederzeit um. Parameter-Mapping
  IMMER über parmeta-NAMEN (nie Indizes), Typ-Semantik aus items-Strings.
- Live 12 nennt den EQ-Q-Parameter `"{n} Q A"`, NICHT „Resonance A"
  (Alias behalten). Wertesemantik verifiziert: Frequency-Norm →
  Hz = 10·2200^v, Q-Norm → 0.1·180^v, Gain direkt dB.
- EQ8-Anzeige-Kurven sind gegen Lives Display KALIBRIERT
  (docs/TouchLive.md §10j, < 0.4 dB): analoge RBJ-Prototypen mit
  Q_eff-Gesetzen pro Typ (Bell `Q·0.5151·10^(0.04908·|g|)`, Shelf
  eigene Formel, Cuts/Notch 1:1, 48er = Butterworth-8-Kaskade ×
  `(1.097+0.611·lgQ)`); „Adaptive Q" aus parvals schaltet den
  Gain-Term. Formeln NIE ohne neue Messkampagne ändern; Y-Range
  ±15 dB wie Live.
- EQ8-Gesten (§10j): Punkt halten + Antippen = Mehrfachauswahl;
  Pinch-Q NUR bei berührtem Punkt; frei: 2 Finger = Auswahl schieben,
  3 = Output, 4 = Scale; Primär-Release → Restfinger idle; Long-Press
  (~1 s still) = Typ-Selector (wischen wählt, loslassen übernimmt).
  Band-Drag ist RELATIV mit Schwelle — nie zur Fingerposition
  springen lassen. Kernpfade touchDown/Move/Up — Maus-Handler NIE
  eigene Logik geben.
- EQ8-Scale skaliert die BAND-GAINS (clamp ±15, Cuts/Notch unberührt,
  negativ invertiert; Range −2..+2) — in der Anzeige-Kurve
  eingerechnet, Punkte bleiben beim nominellen Gain (§10j).
- Spektrum (§10k, LiveSpectrumTap): FFT NUR auf dem Message Thread;
  SPSC mit genau EINEM Producer (Link-Thread ODER Audio-Thread,
  Moduswechsel stoppt erst die alte Quelle); LinkClock als
  WeakReference; Averaging ist LOKAL (nie an Lives Regler binden).
- Thinning-Kanal von sendTouchValue = Adresse + Argumente OHNE den Wert
  (`touchKeyFor`) — NIE auf die nackte Adresse zurückbauen, sonst
  latchen sich Freq+Gain bzw. Multi-Touch-Fader gegenseitig weg.

Live-Remote-Bridge (§10l, 17.07.2026):
- `Source/TouchLive/LiveRemoteBridge` (EngineProcessor-Member, headless):
  AlphaTrack bedient den in Live SELEKTIERTEN Track. Wert-Beobachtung =
  TICK-DIFFING gegen lokale Caches (keine ValueTree-Listener am Modell);
  Motor = zweite `PositionFeedbackRouter`-Instanz; LEDs folgen dem
  MODELL, nie dem Tastendruck; eigene Fader-Sends aktualisieren die
  ANZEIGE sofort lokal (`localValue`, das Modell bleibt waehrend der
  Suppression alt — nie aufs Echo warten, §5.1).
- transport-Domain traegt seit 07/2026 `bar`/`beat` (beat-QUANTISIERT in
  collect() = Diff-Drossel; der current_song_time-Listener ist einzeln
  try/except gebunden). Beim Erweitern die Quantisierung nicht aufweichen.
- Mute/Solo/Arm/Track-Wechsel via sendCommand mit INT-Args (nie Bool);
  Fader via sendTouchValue + noteTouchedParameter.
- **Lokaler Zwei-Controller-Sync (§5.1):** Bridge UND Mixer-View haengen am
  SELBEN `LiveSetModel`. Wer sendet, schreibt seinen Wert OPTIMISTISCH ins
  Modell (`LiveSetModel::setItemField`/`setItemArrayElement`; UI ueber
  `TouchLiveClient::applyLocalMixerValue`/`...ArrayElement`, das Modell +
  Suppression buendelt) — sonst haelt die Echo-Suppression das Modell alt und
  der ANDERE lokale Controller sieht die Aenderung nie (Feldtest 17.07.2026:
  AlphaTrack-Motor und Conduit-Fader liefen nur bei Live→Conduit synchron).
  Die Suppression verwirft danach nur noch Lives redundantes Echo. Selbst-
  Feedback ist entschaerft: `TouchLiveFader` ignoriert `setRemoteValue`
  waehrend der Geste, Pan/Send-Slider setzen mit `dontSendNotification`.
- **Motor-Latch (Bridge):** der eigene Fader-Send haelt Motor+Anzeige
  (`localValueLatched`), bis das Modell nachweislich driftet
  (`releaseLatchOnModelDrift`, je tick) — ein reines Zeitfenster liesse den
  Motor beim Loslassen auf die Ausgangsposition zurueckfahren, wenn Lives Echo
  waehrend der Suppression nie eintrifft.
- AlphaTrack-LCD (`AlphaTrackLcd`): SysEx-Frames NUR als Diff-Runs pro
  tick(); `forceRedraw` + LED-Cache-Reset bei Geraete-/Rollen-Wechsel —
  sonst restauriert das Dedupe nie.

Browser (M4):
- KEINE Domain — Request/Response (`/remote/browser/roots|children` →
  `/remote/browser/list`, gleiche [seq,chunk,chunks,json]-Hülle, Chunks
  konkatenieren das `it`-Array). Node-IDs sind Session-transient (NIE
  persistieren); verlorene Antworten heilt der nächste Tap.
- Browser-Listen IMMER über `Sender.send_json_list` (elementweises
  Listen-Chunking) — Key-Level-Chunking (`send_json`) verwirft große
  Ordner als EINEN oversized `it`-Key (Feldtest 10.07.2026: Drums kam
  leer an).
- Lives Kategorie-Wurzeln (Sounds…Packs) melden `is_folder=False`,
  haben aber Kinder — die View öffnet ALLES, was nicht ladbar ist.
- `TouchLiveClient::onReconnected` feuert bei JEDEM Übergang zu
  connected (neben subscribeAll): die BrowserView verwirft dann alle
  Ebenen (tote Node-IDs nach Live-Neustart) und re-requested die Wurzeln.
- Laden zielt auf Lives Track-Selektion (`/live/song/set/selected_track`).

Meter-Pfad (M2):
- `/remote/meters` = flache Tripel `[id:str, left:float, right:float]` —
  bewusst KEIN Domain-Diff, KEINE seq (Frames idempotent), Stille-Dedupe;
  Werte sind Lives rohe output_meter-Norm. Devices mit `gain_reduction`
  hängen `[dv-id, gr, gr]` an (Push-Vorbild). Subscription
  `/remote/meters/subscribe`, Heartbeat-Timeout beendet den Stream.
- `TouchLiveMeterBus` NIE in den ValueTree; Meter sind ROH (kein Slew,
  keine Echo-Suppression, §5.1); UI liest per Frame-Zähler im
  UiFramePacer-Tick (nativ, global gedrosselt — Rule ui-design) und
  nur bei sichtbarer Page. `clear()` erhöht den Frame-Zähler.
- pytest der Gegenseite: lokal kollidieren Manager-Tests mit laufendem
  Live (Port 9010) — CI-Job `remote-script` läuft immer vollständig.

LOM-Fallen (Feldtest 09.07.2026, docs/TouchLive.md §10d — im Script NIE
zurückbauen):
- LOM-Wrapper sind NICHT identitätsstabil — Stable-IDs IMMER über
  `_live_ptr` (stable_ids._identity), nie über `id(obj)`.
- `track.arm` wirft auf nicht armbaren Tracks, `mute`/`solo` auf dem
  Master, `output_meter_*` auf Tracks ohne Audio-Ausgang — Zugriff UND
  Listener-Binding nur mit Fähigkeits-Guard/try-except; fehlende
  Fähigkeit = Key weglassen.
- Der Test-Stub simuliert diese Fallen (stub_live.Track wirft) —
  Stub-Realismus beim Erweitern erhalten.
- **NIE Background-Threads im Remote Script** (Feldtest-Messung: Lives
  embedded Python schedult sie nur im ~100-ms-Tick, GIL — 111-ms-Stufen).
  Hochrate NUR über `Live.Base.Timer` → `OscServer.pump()` [Main Thread];
  ohne Timer automatischer Tick-Raten-Fallback (`pump_active`).
- Listener-Binden kann pro Live-Version werfen (12.4b:
  add_scenes_listener, „Observer already connected" bei Clip-Listenern) —
  `Domain.attach()` hat einen generischen Poll-Fallback; beim Erweitern
  neuer Domains NICHT umgehen, Listener-Binds einzeln try/except.
- Compressor-`gain_reduction` existiert in 12.4b NICHT im Python-LOM
  (dir()-bewiesen, docs/TouchLive.md §10g) — Push nutzt den proprietären
  Display-Kanal. GR-Code bleibt (greift, falls später verfügbar);
  echter Weg wäre ein M4L-GR-Tap-Device.
