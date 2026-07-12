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

## M1a — Registry + Matching (07/2026)

`Source/Core/MidiRigSettings.h/.cpp`: `RigDevice` (id `juce::Uuid`,
label, `RigDeviceKind` soundGenerator|controller, midiOutName,
midiInName) in dynamischer Liste, ValueTree↔XML-Persistenz Muster
`LooperSettings` in eigener Datei `Conduit/MidiRig.settings`. App-Zustand
(ADR E3) — bewusst KEIN fixes Array wie bei Loopern, da die Gerätezahl
user-bestimmt ist; jedes Gerät trägt eine stabile Uuid für Referenzen
aus späteren Meilensteinen (Profile-Zuordnung, Macro-Ziele).

`Source/Core/MidiPortHub.h/.cpp`: `midirig::resolvePortName()` als reine
Matching-Funktion (exakt→Prefix, Suffix „ (n)" auf EINER der beiden
Seiten ignoriert) — bewusst KEIN zweites persistiertes Prefix-Feld wie
bei `CalibrationProfile`, die Fallback-Logik rechnet zur Matching-Zeit.
`MidiPortHub` cached verfügbare MIDI-In/Out-Devices über einen
injizierbaren `DeviceListProvider` (Default = echte JUCE-Aufrufe,
Tests injizieren Fake-Listen) und matcht `RigDevice`-Portnamen dagegen.
**Noch KEIN Port-Öffnen, keine SpscQueue, kein NRPN/PC** — das folgt in
M2, wo die Threading-Invariante (Rule `midirig`, ADR E4/E7) erstmals
greift; M1 bleibt Message-Thread-only ohne Hardware-I/O.

`EngineProcessor` hält `midiRigSettings`/`midiPortHub` als Member
(Muster `looperSettings`, `redirectSettings`-Helper) mit Gettern.

## M1b — Hub-Kern: Portbetrieb, Queues, Drain, Ablösung (07/2026)

Der Hub ist vom Resolver zum PORT-BESITZER geworden; die Ein-Port-
Klassen MidiControlInput/MidiNoteInput (Grid Block G/H4) sind entfernt.

- **Event-Typen** (`Source/Core/MidiControllerEvent.h`):
  `midi::ControllerEvent {kind: cc|nrpn|programChange; channel; number;
  value; is14Bit; isRelative}` exakt nach ADR E4 (M1b füllt nur kind=cc,
  Felder vollständig — kein zweiter Migrationsschritt in M2) +
  `midi::NoteEvent {note; velocity; isOn}`.
- **Portbetrieb:** `syncFromRegistry()` öffnet/schließt pro RigDevice
  die aufgelösten In-/Out-Ports (exakt→Prefix); Re-Sync automatisch bei
  Registry-Broadcast UND USB-Reconnect (`MidiDeviceListConnection`);
  nicht auflösbar = Zustand „nicht verbunden" pro Gerät
  (`isInputConnected`/`isOutputConnected`), kein Fehler-Spam.
- **Queues (Rule midirig/E4):** pro offenem In-Port eine EIGENE
  `SpscQueue<ControllerEvent>` UND `SpscQueue<NoteEvent>` (je 512);
  EIN zentraler 60-Hz-Drain [MT] mit Fanout über
  `subscribeController(deviceId)/subscribeNotes(deviceId)/
  subscribeTick()` + Token-`unsubscribe`. M1b verwirft alles außer
  CC/Noten (NRPN/PC-Assembler folgt M2, VOR dem Queue-Push).
- **Überlauf „Latest-Pending" (User-Entscheidung 12.07.2026):** volle
  Controller-Queue → neuestes Event in einen atomaren Ein-Slot-Puffer
  (uint64-gepackt, neuere überschreiben); Producer schiebt VOR dem
  nächsten Event nach (Reihenfolge gewahrt), der Drain leert den Slot
  am Tick-Ende. Kein Blockieren, finaler Controller-Wert garantiert;
  strenges „Ältestes verwerfen" wäre mit der SpscQueue nicht SPSC-rein
  (Producer-pop = Data Race). Note-Queues: drop-newest (M0-Parität).
- **Senden:** `send(deviceId, msg)` + stabile `IMidiOutputTarget`-
  Fassaden pro Gerät (`outputTargetFor`) und die Rollen-Fassade
  `gridOutputTarget()` (löst die Grid-Ausgangs-Rolle bei jedem send()
  live auf — MpeMidiSink/GridPage/MacroPanel binden daran, Rollen-
  Wechsel überleben). `MidiDeviceTarget` lebt als realer
  Output-Port-Handle im Default-Opener weiter.
- **Grid-Rollen + Migration:** `MidiRigSettings` trägt
  `gridControllerDeviceId`/`gridOutputDeviceId` (persistent);
  Einmal-Migration der GridPanelSettings-Namen (controlIn → Gerät
  „Controller", gridOut+echoIn → Gerät „Grid-Ausgang"), Flag
  `migratedFromGridPanel` verhindert den Zweitlauf, Quell-Strings
  bleiben unangetastet. Die drei Grid-Settings-Combos (MPE-Out/
  Controller-In/Echo-In) schreiben jetzt die Registry-Rollen-Geräte.
- **Settings-UI:** Tab „MIDI" im Einstellungen-Fenster
  (`Source/UI/MidiRigSettingsComponent`): Geräteliste Name (editierbar)/
  Rolle/In/Out/Verbunden-LEDs, Add/Remove, 44-px-Zeilen; abgesteckte
  Ports erscheinen als „(getrennt)" statt still auf „—" zu fallen.
- **Test-Seams:** injizierbare `InputPortOpener`/`OutputPortOpener`
  (Fake-Handles, Tests spielen Messages direkt in den registrierten
  `MidiInputCallback` ein) + `drainNow()` als manueller Drain.

## Lektionen

- **MSVC + verschachtelte Brace-Init:**
  `juce::Array<juce::MidiDeviceInfo> { { "name", "id" } }` deutet MSVC
  als `Array<const char*>`-Initialisierung (Fehler C2665 tief in
  ArrayBase) — immer explizit
  `juce::MidiDeviceInfo ("name", "id")` schreiben.
- **SpscQueue kann kein „Ältestes verwerfen":** `pop()` gehört exklusiv
  dem Consumer — ein Producer-seitiges Verdrängen wäre ein Data Race.
  Latest-Pending (atomarer Ein-Slot-Überlaufpuffer, s. o.) liefert die
  musikalisch relevante Eigenschaft (finaler Wert kommt an) SPSC-rein.
- **`juce::Uuid()` ist RANDOM, nicht null** — für „Rolle unbesetzt"
  explizit `juce::Uuid::null()` initialisieren und über
  `indexOfId() < 0` prüfen, nie über Null-Vergleich mit einem
  Default-konstruierten Uuid.

## Meilensteinleiter

  M1  MidiPortHub + Registry — M1a Registry+Matching (erledigt 07/2026) + M1b Hub-Kern: Portbetrieb/Queues/Drain/Ablösung der Ein-Port-Klassen + Migration + Settings-UI (erledigt 07/2026)
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
