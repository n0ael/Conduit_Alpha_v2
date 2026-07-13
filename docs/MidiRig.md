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

## M2 — Profile + NRPN + PC (07/2026)

User-Entscheidungen 12.07.2026: Hardware-Tab im Macro-Panel schon in M2
auf CSV-Profile inkl. NRPN erweitert (feldtestbar); Factory-CSVs =
Analog Heat + die 5 bestehenden Klartext-Geräte.

- **CSV-Profile (ADR E1):** `Source/Core/MidiDeviceProfile.h/.cpp` —
  toleranter, Header-Zeilen-getriebener midi.guide-Parser (Spalten per
  Namen case-insensitiv, unbekannte ignoriert, RFC-4180-Quoting;
  `ParseReport` zählt accepted/skipped + Warnungen). `ProfileParam`
  trägt cc UND/ODER nrpn (msb*128+lsb) + min/max (NRPN-Range hat bei
  Misch-Params Vorrang, kaputte Ranges werden repariert).
- **Library:** `Source/Core/MidiProfileLibrary.h/.cpp` — Factory-CSVs
  aus BinaryData (`Assets/DeviceProfiles/*.csv`, via
  `namedResourceList`+`originalFilenames` nach Endung gefiltert) +
  User-Ordner `Conduit/Devices/**/*.csv` (rekursiv). Ein User-Profil
  mit gleichem manufacturer+device ERSETZT das Factory-Profil KOMPLETT
  (kein stiller Merge — User-Hoheit, Rule midirig). Pro Quelle ein
  `SourceReport` für die UI (E1b). EngineProcessor-Member, Ordner aus
  `MidiRigSettings::settingsFile().getSiblingFile("Devices")`.
- **NRPN-Empfang (ADR E4):** `Source/Core/NrpnAssembler.h` — pure
  Zustandsmaschine PRO PORT (im `InputConnection`, MIDI-System-Thread,
  VOR dem Queue-Push), Zustand pro Kanal. CC99/98 = Adresse (aktiviert),
  CC101/100 = RPN (deaktiviert), CC6 → Event {value=msb, is14Bit=false},
  CC38 → Event {value=(msb<<7)|lsb, is14Bit=true} — eine 14-bit-Fahrt
  erzeugt ZWEI Events, der letzte gewinnt (Dedupe beim Konsumenten).
  CC6/38 ohne aktive Adresse = passthrough (Plain-Data-Entry-Geräte).
  **CC96/97 (Inc/Dec) bewusst out of scope** (passthrough).
- **Program Change (ADR E5):** Empfang → ControllerEvent
  {kind=programChange, number=Programm} (Trigger-Quelle, Konsumenten
  folgen); Senden über `MidiProgramChangeTarget` (optional Bank
  CC0/CC32 vor dem PC, Dedupe).
- **NRPN-Senden:** `MidiNrpnTarget` (MacroBindings) — value01 →
  [min,max] aus dem Profil, Dedupe auf dem gemappten Wert, Sequenz
  CC99→98→6→38 bei JEDEM send() komplett (parallel sendende CC-Ziele
  können die Geräte-Adresse nicht verfälschen). Persistenz:
  Tree "NrpnTarget" {channel, number, min, max, name} bzw. "PcTarget"
  {channel, bankMsb, bankLsb} — Zweige in GridPage::makeTargetFromState.
- **Hardware-Picker (Macro-Panel):** Device-Dropdown = Klartext-DB
  (Block L2) + Profile (Ids fortlaufend hinter der DB); Profil-Params
  zeigen "Section: Name (CC n | NRPN n)"; NRPN-Param → MidiNrpnTarget,
  CC-Param → MidiCcTarget. Klartext-Schnellpfad bleibt (E1b), abschaltbar
  über `MidiRigSettings::useLegacyCcList` (Toggle im MIDI-Tab; wirkt beim
  nächsten GridPage-Aufbau).
- **MIDI-Tab Sektion „Profile":** Lade-Report je Quelle
  (Factory/User · Geräte · Parameter · übersprungen + Warnungen),
  „Neu laden", Legacy-Toggle, Attribution-Fußzeile.

**Feldtest (13.07.2026, User-Hardware):** NRPN-Sendeweg gegen Elektron
Analog Heat (MK1) verifiziert — Wire-Daten bit-genau bis 16383, exakt
deckungsgleich mit einer unabhängigen Ableton-MIDI-Map-Steuerung
(anderes Protokoll/Codebasis). Das beobachtete „Drive bleibt bei der
Hälfte hängen"-Verhalten ist ein **Firmware-Bug im Heat selbst**
(Wert-Wraparound nahe dem Maximum, nur im Standard-MIDI-CC/NRPN-Pfad,
NICHT über Overbridge oder reine Handbedienung) — kein Conduit-Fund,
kein Fix nötig. Zusätzlich mit einem Dave Smith Mopho gegengetestet
(User-Profil aus dessen eigenem Max-Patch extrahiert, siehe Lektion
unten) — nach Korrektur des Profil-Wertebereichs sauber durchlaufend.

**Attribution (ADR E1):** Die Factory-Geräteprofile in
`Assets/DeviceProfiles/` stammen von **midi.guide**
(pencilresearch/midi, GitHub) und stehen unter **CC-BY-SA 4.0**.
Ein App-About-Dialog existiert noch nicht — die Attribution lebt hier
und als Fußzeile der Profile-Sektion im MIDI-Settings-Tab.

## M3 — Semantischer Picker (07/2026)

Ersetzt die zwei flachen `ComboBox`en des Hardware-Ziel-Pickers im
Macro-Panel (Geräte-Liste + Parameter-Liste als Text, ~11 Geräte /
~400 Parameter bereits heute) durch einen echten Drill-down — Analogie
Ableton-Parameter-Browser Track→Device→Parameter.

- **`Source/Core/MidiTargetBrowserModel.h/.cpp`** — headless, pure,
  Catch2-testbar: `Kind{manufacturer, device, section, parameter}`,
  `enter()`/`goBack()`/`breadcrumbText()`/`setFilter()`. Quelle Top-
  Ebene: Klartext-DB-Geräte (`HardwareCcDatabase`, E1b) erscheinen
  UNGRUPPIERT (kein Manufacturer-Feld), CSV-Profile gruppieren unter
  ihrem `manufacturer`. Section-Ebene nur, wenn ein Gerät überhaupt
  Sections hat (Analog Heat: nein → direkt Parameter; Blofeld/Digitakt:
  ja); Parameter mit leerer Section innerhalb eines sonst sektionierten
  Geräts landen in einem synthetischen "Allgemein"-Sammeltopf.
  `setFilter()` durchsucht rekursiv ALLE Parameter unterhalb der
  aktuellen Ebene (flache Trefferliste, Substring case-insensitiv);
  Navigation (`enter`/`goBack`) setzt den Filter zurück.
- **`Source/UI/HardwareTargetPicker.h/.cpp`** — CallOutBox-Inhalt
  (Muster `TrackSelectorPanel`: schließt sich nach Auswahl selbst über
  `findParentComponentOfClass<CallOutBox>()->dismiss()`). Breadcrumb-
  Kopfzeile (Zurück-`IconTile`, Muster `Browser/BrowserPanel`) +
  custom-painted, scrollbare Zeilenliste (kein `ListBox` nötig bei den
  hier realistischen Zeilenzahlen) + Suchfeld unten mit `TouchKeyboard`
  (Fokus-Falle beachtet: Tasten greifen nie den Fokus). Feste
  Gesamthöhe (320×480) — die Tastatur verkleinert nur den Listen-
  bereich, kein CallOutBox-Resize während der Navigation nötig.
- **`Source/UI/MacroPanel.h/.cpp`**: `hwDeviceCombo`/`hwParamCombo`
  ersetzt durch eine einzelne `hwSummaryTile` (zeigt `describe()` des
  aktuellen Ziels), Tap öffnet den Picker. `createHardwareTarget()`
  nimmt jetzt eine `MidiTargetBrowserModel::Row` entgegen statt zwei
  Combo-Indizes zu lesen — Baulogik (CC- vs. NRPN-Ziel) unverändert.
- **Nebenbei gefundener + gefixter Bug (M2-Erbe):**
  `TargetRow::rebuildFromBinding()` erkannte beim Session-Neuladen NUR
  `MidiCcTarget`/`AbletonParamTarget` per `dynamic_cast` — ein geladenes
  `MidiNrpnTarget`/`MidiProgramChangeTarget` (z. B. aus dem Analog-Heat-
  Feldtest) fiel in den `else`-Zweig und landete fälschlich im
  Live-Zustand mit leeren Combos. Jetzt beide Typen erkannt; dafür neue
  Getter `MidiNrpnTarget::rangeMin()/rangeMax()/name()` und
  `MidiProgramChangeTarget::channel()` (fürs Kanal-only-Rebuild eines
  geladenen Ziels ohne erneute Picker-Auswahl).

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
- **`nrpn_max_value` ist NICHT pauschal 16383:** der generische
  14-bit-Default gilt nur, wenn das Gerät den Parameter wirklich über
  den vollen Bereich anspricht (Analog Heat). Geräte mit einem engeren
  realen Wertebereich (Mopho CutOff: 0–164, aus Handbuch UND eigenem
  Max-Patch bestätigt) müssen `nrpn_max_value` auf diesen realen Wert
  setzen — sonst sendet Conduit über den Fader-Weg fast durchgehend
  Werte oberhalb des Geräte-Maximums, das Gerät kappt sie, und der
  Regler wirkt nur im untersten Bruchteil des Fader-Wegs (beobachtetes
  Symptom: Fader bewegt sich, Klang springt nur ganz unten von 0 auf
  den Gerätemax-Wert). Quelle für den realen Bereich: Handbuch ODER
  (verlässlicher, da keine PDF-Tabellen-Transkription) eine vorhandene
  eigene Max-Patch-/Editor-Datei des Geräts, siehe `parameter_mmax` der
  `live.dial`/`live.numbox`-Objekte.
- **Herstellerhandbuch-PDFs mit zweispaltigen NRPN-Tabellen** extrahieren
  über generische HTML-Textstrippung (ManualsLib o. ä.) unzuverlässig —
  Spalten werden separat gelesen und beim Zusammenfügen verrutschen
  Parameter↔Nummer-Zuordnungen. Eine echte Max-for-Live-Patch-Datei
  (`.amxd`, ab Byte-Offset des ersten `{` meist gültiges JSON, Rest ist
  ein binärer Ressourcen-Anhang) ist die verlässlichere Quelle, wenn der
  User eine besitzt: Boxen (`maxclass: live.dial/live.numbox`,
  `saved_attribute_attributes.valueof.parameter_longname`) über
  `lines`/`patchline`-Quellen/Ziele bis zur `---nrpn <bank> <nummer>`-
  Message-Box verfolgen (inkl. Sprung über Subpatcher-Grenzen via
  `inlet`-Objekte).

## Meilensteinleiter

  M1  MidiPortHub + Registry — M1a Registry+Matching (erledigt 07/2026) + M1b Hub-Kern: Portbetrieb/Queues/Drain/Ablösung der Ein-Port-Klassen + Migration + Settings-UI (erledigt 07/2026)
  M2  Profile + NRPN + PC — midi.guide-CSV-Parser (Klangerzeuger-Profile), NRPN-Assembler pro Port, Program-Change Senden/Empfangen — erledigt 07/2026 (inkl. Hardware-Picker-Vorgriff: NRPN/CC-Ziele aus Profilen)
  M3  Semantischer Picker — Geräte-/Parameter-Auswahl-UI (Analogie Ableton-Parameter-Browser Track→Device→Parameter) — erledigt 07/2026
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
