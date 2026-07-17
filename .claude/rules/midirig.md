---
paths:
  - "Source/Core/Midi*"
  - "Source/Core/HardwareCcDatabase*"
  - "docs/MidiRig.md"
---

# Rule: midirig — MIDI-Rig-Subsystem (CLAUDE.md 10.0, ADR 006)

**Pflichtlektüre: docs/MidiRig.md** (Spezifikation + Meilensteinleiter).

- Queue-Topologie (E4): jeder Eingangsport hat MIDI-seitig einen
  eigenen System-Thread → EINE `SpscQueue` PRO PORT, zentraler 60-Hz-
  Drain auf dem Message Thread. Nie mehrere Producer auf eine
  SpscQueue (CLAUDE.md §3.1).
- NRPN-Assembler ist ein Zustandsautomat PRO PORT auf dem MIDI-System-
  Thread, VOR dem Queue-Push — MSB/LSB-Paare gehören zusammengesetzt
  in die Queue, nie als rohe CC-Halbwerte.
- Threading-Invariante (E7): das gesamte Subsystem läuft ausschließlich
  MIDI-System-Thread → SpscQueue → Message Thread. Der Audio-Thread ist
  NIE beteiligt.
- Geräte-Matching (Klangerzeuger UND Controller) über MIDI-Port-Namen
  exakt→Prefix (Muster CalibrationProfile, CLAUDE.md §8.1).
- Profil-Dateien (`Conduit/Devices/*.csv`, `Conduit/Controllers/*.csv`,
  `userHardwareDevices.txt`) sind User-Hoheit — nie ungefragt
  überschreiben, nur ergänzen/mergen (Muster HardwareCcDatabase-Merge).
- SysEx bleibt Sende-only (E6) — kein Parsing, kein Feedback, keine
  Checksummen, kein Patch-Editing.
- Shift-Ebenen (M5a): 1:1 bleibt — eine Adresse pro Ebene, Ebenen
  unterscheiden sich NUR über das kanonische Modifier-Set (gehaltene
  Noten). Matching: exakteste Ebene gewinnt (größtes gehaltenes Set);
  Note-Off geht an die per On gelatchte Ebene, nie an die aktuell
  passendste. Fan-out auf mehrere Ziele ist Sache der Macros, nie der
  MidiInBindings.
- Pickup (M6): die physische Position ist PRO ADRESSE geteilt
  (MidiInBindings::physicalPositions) — jedes CC-Event disengaged die
  Geschwister-Ebenen derselben Adresse (Note-Toggles NIE, deren Press
  würde verschluckt). waiting/Blink nur für CCs mit BEKANNTER Position;
  `kPickupEpsilon` ist die EINE Konstante für Gate und LED-Aussage.
  LED-Übersetzung lebt ausschließlich im profil-getriebenen
  `PickupLedRouter` (CSV: `group` + meanings status_red/status_amber/
  status_green/led_pickup) — der Echo-Pfad überspringt diese meanings,
  der Router restauriert beim Verlassen aus dem Echo-Cache (nie blind 0
  senden). `setPickupEnabled` ist idempotent (läuft bei jedem
  Registry-Broadcast). M6.1: die Shift-Pad-Anzeige (Mechanismus 4) ist
  SOLID + richtungskodiert (rot=verringern/orange=erhöhen/grün=gefunden,
  `led`/`led_layer_b`/`led_layer_c`), nicht blinkend; der Näherungswert
  bleibt die Spalten-Status-LED. `PickupState` trägt `physicalAbove`/
  `aligned`; aligned-Einträge sieht nur Mechanismus 4, `waitingFor()`
  filtert sie für 1–3 aus.
- Live-Remote-Bridge (docs/TouchLive.md §10l): Rolle
  `liveRemoteDeviceId`; Grid- UND Live-Rolle auf demselben Geraet →
  Bridge inaktiv (doppelte Fader-Konsumenten). Bridge-Controls werden
  ueber CSV-Control-IDs aufgeloest (fader/pan/f1..f4/shift/track_l/r/
  mute/solo/rec_arm — Konvention in docs/MidiRig.md), NICHT ueber die
  `role`-Spalte (die behaelt layer_select). Profil-Spalte `display`
  (z. B. alphatrack_lcd) waehlt den Display-Treiber datengetrieben.
- Relativ-Encoder (M8.1): die Kodierung ist GERAETEABHAENGIG und kommt aus
  dem Profil (CSV `rel_encoding`: signbit/binoffset/leer=twosComplement) —
  nie im Code pro Geraet verzweigen. EINE Quelle: `RelativeEncoding.h`
  (`decodeRelativeDelta`); `ChannelStripLayers::decodeSignedDelta` ist nur
  noch ein Zweierkomplement-Wrapper. Die Kodierung MUSS beide Konsumenten
  erreichen: Bindungen (`setAddressMode`) UND Ebenen-Selektoren
  (`ChannelStripLayers::feed`). AlphaTrack = signBit, K1 = twosComplement.
- Motorfader/Ribbons (M8): Pitch-Bend-Bindungen leben als Nummer
  `128 + Kanal` (`grid::pitchBendBindingNumber`) im CC-Namensraum — die
  PB-ADRESSE ist der Kanal (findBySendAddress matcht PB ueber
  `sendChannel`, kanal-agnostische M4b-Regel gilt dort NICHT). Adress-Modi
  (absolute/direct/scrub/relativeTicks) sind profil-getrieben
  (`setAddressMode`, GridPage::rebuildAddressModes): `direct` =
  position-Feedback (Motor), wartet NIE (Pickup-Exemption); scrub/
  relativeTicks wenden Deltas OHNE Takeover/Glaettung auf die aktive Ebene
  an. Positions-Feedback sendet AUSSCHLIESSLICH der wert-getriebene
  `PositionFeedbackRouter` (60-Hz-Diff, touch-gated, Basiswert — nie der
  M5c-Effektivwert); der Echo-Pfad ueberspringt `position`-meanings und
  pitchBend-Feedback. Touch-Noten (`touch_number`, `type=touch`) sind NIE
  Bindungs-Quellen — GridPage filtert sie vor Learn/Bindungen (Learn-Falle:
  der Griff zum Fader bindet sonst die Touch-Note).
- Channelstrip-Ebenen (M7): die 4 Top-Encoder (CSV `role=layer_select`)
  wählen pro Spalte (`group`) eine von 3 Binding-Bänken. 1:1 bleibt — eine
  Adresse pro (Spalte, Ebene); `bestMatch` filtert geebente Bindungen auf
  die aktive Ebene, Bänke koexistieren (Dedup schließt column+layer ein).
  Ebenen-Auflösung profil-getrieben über den `columnResolver` (Besitzer
  GridPage) — MidiInBindings bleibt profil-agnostisch (`column` opaker
  String). Ebenen-Anzeige lebt im `PickupLedRouter` als DAUERHAFTE Basis
  (`setColumnLayer`, alle Spalten-Pads solid in Ebenen-Farbe; aktives Pad
  8tel-, Ebenen-Wechsel 16tel-Blink, tempo-synchron via `setBeatPosition`/
  LinkClock) — die Pickup-/Detail-/Shift-Mechanismen 1–4 sind momentane
  Overrides darüber. Der Echo-Pfad (`onFeedbackEcho`) MUSS Router-verwaltete
  LEDs via `isManaging` überspringen (sonst überschreibt das Echo die
  Router-Farbe, Dedup korrigiert nie). Persistenz-Property `stripLayer` NIE
  `layer` (Kollision mit keyToState).
