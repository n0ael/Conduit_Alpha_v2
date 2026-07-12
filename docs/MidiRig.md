# MIDI-Rig — Subsystem-Dossier
> Neu angelegt Juli 2026 (ADR 006). Für Arbeiten an diesem Subsystem
> verbindlich wie die CLAUDE.md selbst (§1.1).

## Zweck

MIDI-Hardware app-weit als vollwertiges Rig behandeln — Klangerzeuger-
Profile (CC/NRPN/Program-Change-Namen statt Rohnummern) UND
Controller-Profile (Send- und Feedback-Adressen für Fader/Pads/
Encoder), ein gemeinsames Transport-Modell (`midi::ControllerEvent`)
und eine zentrale Registry (`MidiRigSettings`). Entscheidungsgrundlage:
**ADR 006**.

Verwandt, aber NICHT Teil dieses Dossiers: der Grid-Baukasten
(docs/Grid.md) nutzt MIDI-Rig-Bausteine als Quelle/Ziel (MPE-Output,
MidiControlInput/MidiNoteInput, HardwareCcDatabase-Vorläufer aus Block
G/H4/L2) — deren Migration unter dieses Subsystem ist Teil der
Meilensteinleiter unten, kein automatischer Claim.

## Meilensteinleiter

  M1  MidiPortHub + Registry — MidiRigSettings (ValueTree↔XML, Muster LooperSettings), Geräte-Matching Port-Name exakt→Prefix (Muster CalibrationProfile) — offen
  M2  Profile + NRPN + PC — midi.guide-CSV-Parser (Klangerzeuger-Profile), NRPN-Assembler pro Port, Program-Change Senden/Empfangen — offen
  M3  Semantischer Picker — Geräte-/Parameter-Auswahl-UI (Analogie Ableton-Parameter-Browser Track→Device→Parameter) — offen
  M4  Controller-Profile + LED — Conduit-Controller-Profile-v1-CSV-Schema, Send-Adresse + bis zu 3 Feedback-Adressen pro Control — offen
  M5  Map-Modus + Tab + Chord-Learn — Zuweisungs-UI, Mehrfachbelegung/Akkord-Learn — offen
  M6  Pickup-LED + Verhalten — Soft-Takeover-Feedback über Controller-Profile-LEDs — offen
  M7  Bidirektional Ribbons — Motorfader-/Ribbon-Feedback in beide Richtungen — offen
  M8  SysEx-Snippets — Sende-only Hex-Snippets mit optionalem `{v}`-Platzhalterbyte — offen

## Referenzen

- ADR 006 (Entscheidungen E1–E7: Profile-Formate, Registry, Transport,
  Threading).
- Rule `midirig` (.claude/rules/midirig.md) — mechanische Invarianten,
  lädt bei Arbeit an `Source/Core/Midi*` / `Source/Core/HardwareCcDatabase*`.
- docs/Grid.md Block G/H4/L2 — bestehender Code-Vorläufer
  (MidiControlInput, MidiNoteInput, MidiInBindings, HardwareCcDatabase),
  bis zur Migration weiterhin dort beheimatet.
