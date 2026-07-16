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
