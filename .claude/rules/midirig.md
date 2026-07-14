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
