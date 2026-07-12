### ADR 006: MIDI-Rig-Subsystem — Klangerzeuger-/Controller-Profile, Transport, Threading

Status: Akzeptiert — Juli 2026

Kontext:
Der Grid-Baukasten (ADR 002/003) bindet heute MIDI-Hardware nur
punktuell an (MidiControlInput/MidiNoteInput als Grid-Blocks G/H4,
HardwareCcDatabase als Block L2 mit Klartext-CC-Tabellen). Die
Klartext-CC-Datenbank deckt reine CC-Geräte ab, aber weder NRPN noch
Program Change/SysEx noch Controller-Feedback (LEDs, Motorfader). Um
MIDI-Hardware app-weit (nicht nur im Grid-Kontext) als vollwertiges
Rig zu behandeln — Klangerzeuger-Profile UND Controller-Profile,
gemeinsamer Transport, ein Registry — braucht es ein eigenständiges
Subsystem statt eines weiteren Grid-Blocks.

Entscheidung:

E1  Klangerzeuger-Profile: midi.guide-CSV nativ als Laufzeitformat,
    Ordner `Conduit/Devices/{Hersteller}/{Gerät}.csv` (User-Datenordner
    neben den `*.settings`-Dateien). Toleranter Parser — unbekannte
    Spalten werden ignoriert, kein hartes Schema-Matching. Factory-
    Geräte werden als CSV mitgeliefert (Analogie BinaryData-Muster aus
    HardwareCcDatabase, Block L2). Lizenzhinweis: midi.guide-Daten sind
    CC-BY-SA 4.0 lizenziert — Attribution gehört ins About-Dialog UND
    in dieses Dossier.

E1b Zweistufig: `Conduit/userHardwareDevices.txt` bleibt als CC-only-
    Schnellpfad erhalten (eigener Settings-Schalter), Klartext-Format
    `[Gerät]` / `n = Name` (Muster HardwareCcDatabase, Block L2). Ein
    Validierungsreport erscheint in der UI beim Laden — kein stilles
    Scheitern bei Parse-Fehlern.

E2  Controller-Profile: eigenes CSV-Schema „Conduit Controller Profile
    v1", Ordner `Conduit/Controllers/{Gerät}.csv`. Pro Zeile ein
    Control: id, typ (knob|fader|pad|encoder), Send-Adresse, bis zu 3
    Feedback-Adressen mit Bedeutung (z. B. LED-Ring, Motorfader-
    Position, Display-Text).

E3  Registry: `MidiRigSettings` als ValueTree↔XML (Muster
    LooperSettings), Datei `Conduit/MidiRig.settings`. Geräte-Matching
    über MIDI-Port-Namen exakt→Prefix (Muster CalibrationProfile,
    §8.1). App-Zustand, NIE Patch-Zustand — das Rig ist Hardware-
    Setup, kein ValueTree-Subtree im Patch (CLAUDE.md §6).

E4  Transport-Vereinheitlichung: `midi::ControllerEvent`
    `{kind: cc|nrpn|programChange; channel; number; value; is14Bit;
    isRelative}`. Der NRPN-Assembler ist ein Zustandsautomat PRO PORT
    auf dem MIDI-System-Thread, VOR dem Queue-Push (MSB/LSB-Paare
    gehören zusammengesetzt in die Queue, nicht als rohe CC-Halbwerte).
    Threading-Konsequenz aus der SPSC-Invariante (CLAUDE.md §3.1, EIN
    Producer pro Queue): jeder Eingangsport hat MIDI-seitig einen
    eigenen System-Thread → EINE `SpscQueue` PRO PORT, ein zentraler
    60-Hz-Drain auf dem Message Thread (Muster MidiControlInput/
    MidiNoteInput). Nie mehrere Producer auf eine SpscQueue.

E5  Program Change: Senden (mit optionaler Bank-Select-Vorstufe
    CC0/CC32) und Empfangen (als Trigger-Quelle, z. B. für Macro-Ziele)
    laufen im selben `ControllerEvent`-Modell wie CC/NRPN.

E6  SysEx begrenzt auf Sende-Snippets (Hex-Bytes, optional EIN
    `{v}`-Platzhalterbyte für einen laufzeitgesetzten Wert). Kein
    Parsing eingehender SysEx, kein Feedback-Pfad, keine Checksummen-
    Berechnung, kein Patch-Editing über SysEx — dokumentierter
    Out-of-Scope (Analogie CLAUDE.md §12).

E7  Threading-Invariante: das gesamte MIDI-Rig-Subsystem läuft
    ausschließlich MIDI-System-Thread → SpscQueue → Message Thread.
    Der Audio-Thread ist NIE beteiligt (M0-Befund bestätigt — MIDI-Rig
    ist reine Message-Thread-/Engine-Level-Logik wie GridVoiceEngine,
    CLAUDE.md §4.2 ITouchMacro-Analogie, keine Interface-Kategorie mit
    Audio-Thread-Pflicht).

Konsequenzen:
+ Ein Subsystem für alle MIDI-Hardware-Belange (Klangerzeuger UND
  Controller), statt verstreuter Grid-Blocks — HardwareCcDatabase
  (Block L2) und MidiControlInput/MidiInBindings (Block G) wandern
  konzeptionell hierher, bleiben aber code-seitig vorerst an ihren
  bestehenden Pfaden (Migration ist ein eigener Meilenstein, kein
  Big-Bang-Rename in diesem ADR).
+ Der Port-pro-Thread-Befund erzwingt früh die richtige Queue-Topologie
  (E4) — verhindert eine spätere Multi-Producer-SpscQueue-Verletzung.
- CSV als Laufzeitformat statt eines bereits geparsten Binärformats
  bedeutet Parser-Pflege bei midi.guide-Format-Drift; der tolerante
  Parser (unbekannte Spalten ignorieren) mindert das Risiko.
- NRPN-Geräte waren in Block L2 explizit out of scope („noch") — dieses
  ADR löst genau diese Lücke ein, aber SysEx bleibt bewusst
  Sende-only (E6): vollständiges SysEx-Parsing ist ein eigenes, hier
  nicht adressiertes Problem.
- Kein Big-Bang-Rename der bestehenden Grid-Klassen — Migration von
  HardwareCcDatabase/MidiControlInput/MidiInBindings unter das neue
  Subsystem ist Aufgabe der Meilensteinleiter (docs/MidiRig.md), nicht
  dieses ADRs.
