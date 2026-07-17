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

## M4 — Controller-Profile + LED-Feedback (07/2026)

Löst das Gegenstück zu M2 ein: nicht "wie heißt Parameter X am Klangerzeuger",
sondern "welches physische Control AM CONTROLLER sendet was, und was schickt
Conduit zur LED-/Motorfader-Rückmeldung dorthin zurück" — füllt den seit M1b
vorbereiteten, nie verdrahteten Hook `MidiInBindings::onFeedbackEcho`.

User-Entscheidungen (13.07.2026): Profil-Zuordnung über ein neues,
explizit gesetztes `RigDevice`-Feld (`controllerProfileName`, Picker im
MIDI-Tab) statt fragilem Label-Matching; Feedback-Typ `display` wird vom
Schema/Parser bereits erkannt, aber bewusst NICHT gesendet (kein SysEx-Weg
vor M8 — Laufzeit überspringt `meaning == "display"` still); Factory-Profil
für den **Allen & Heath Xone:K1** aus einem User-Chart transkribiert
(nicht midi.guide — eigene Quelle, ADR E1's Attribution gilt hier nicht).

- **`Source/Core/ControllerProfile.h/.cpp`** (namespace `conduit::midirig`)
  — `AddressKind{cc,note}`, `FeedbackAddress{kind,channel,number,meaning}`,
  `ControllerControl{id,type,section,sendKind/Channel/Number,feedback[0..3]}`,
  `ControllerProfile{device,controls}` + `findBySendAddress()` (pure, Kern
  der Laufzeit-Zuordnung). `parseControllerProfileCsv()` — Muster
  `MidiDeviceProfile.h/.cpp` (`splitCsvLine`/`fieldAsInt`/`fieldAsString`,
  header-getriebene Spaltensuche, `ControllerParseReport`), aber EIN Profil
  pro Aufruf (die Ordnerkonvention `Conduit/Controllers/{Geraet}.csv` ist
  bereits pro Gerät benannt, kein Multi-Device-CSV wie bei den
  Klangerzeuger-Profilen).
- **`Source/Core/ControllerProfileLibrary.h/.cpp`** — Muster
  `MidiProfileLibrary.h/.cpp`, aber FLACHER User-Ordner
  `Conduit/Controllers/*.csv` (nicht rekursiv, kein `{Hersteller}/`-
  Unterordner) und das Factory-Profil wird **direkt per BinaryData-Symbol**
  referenziert statt generisch über alle `.csv`-Ressourcen gescannt —
  `originalFilenames` trägt keinen Ordnerpfad, ein genereller Scan würde
  mit den Klangerzeuger-CSVs im selben flachen BinaryData-Namensraum
  kollidieren. Ordner-Basis `midiRigSettings.settingsFile()
  .getSiblingFile("Controllers")` (identisches Muster wie M2s `Devices`).
- **`Assets/ControllerProfiles/AllenHeath_XoneK1.csv`** — 52 Controls:
  16 Encoder (CC0–15, nur die ersten 4 mit Send/Return + LED-Ring-Feedback
  laut Chart, Rest Send-only), 4 Fader (CC16–19, Send-only — echte
  Hardware ist nicht motorisiert), 2 Setup-Encoder (CC20/21), 30 Taster
  (`type=pad`, `send_kind=note`) mit der roten "Send/Return"-Note als
  Send-Adresse und den beiden "Return"-Noten (+3/+6 Oktaven laut Chart)
  als Feedback1/2 (`meaning=led_layer_b`/`led_layer_c`). Notenwerte per
  Konvention C-1=0/C4=60 (G9=127 = MIDI-Maximum, deckt sich exakt mit
  dem höchsten Chart-Wert — bestätigt die Konvention).
- **Registry:** `RigDevice::controllerProfileName` (neues Feld,
  ValueTree-Property `controllerProfileName`) + `MidiRigSettings::
  setControllerProfileName()`. `MidiPortHub::gridControllerOutputTarget()`
  — neue Rollen-Fassade (Muster `gridOutputTarget()`), löst die
  Controller-Rolle live auf.
- **UI (`MidiRigSettingsComponent`):** pro Controller-Zeile ein viertes
  Dropdown (Profil, nur sichtbar bei Rolle "Controller", Layout wechselt
  zwischen 3/4 gleich breiten Combos); zweite Report-Sektion
  "Controller-Profile" unter der M2-Sektion (kein Legacy-Toggle-Äquivalent).
- **Laufzeit (`GridPage`):** `midiInBindings.onFeedbackEcho` einmalig im
  Ctor gesetzt (nicht in `refreshRigSubscriptions()` — löst Rolle/Profil
  bei jedem Aufruf live auf, wie die Output-Fassaden): Controller-Rolle →
  `controllerProfileName` → `ControllerProfileLibrary::find` →
  `findBySendAddress(kind, channel, number)` → für jede Feedback-Adresse
  (außer `display`) eine CC- oder Note-On-Message über
  `gridControllerOutputTarget()`.
- **Note-Bindungen (Feldtest-Fund 14.07.2026, Xone:K1-Pads):** die gesamte
  Macro-Eingangs-Kette war CC-only — Pads (Noten) liefen ins Leere.
  Behoben: `midi::NoteEvent` trägt jetzt einen `channel`;
  `MidiInBindings::Binding` hat `isNote` (CC- und Note-Adressraum getrennt,
  Persistenz-Property `isNote` im GridSessionStore, fehlend = CC —
  rückwärtskompatibel); `handleIncomingNote()` (Momentary + Velocity,
  User-Entscheidung: On = Velocity/127, Off = 0 — Toggle-Verhalten macht
  das Ziel-Control); MIDI-Learn bindet auch die nächste Note (nur Note-On,
  ein streunendes Off löst nicht aus); GridPage abonniert zusätzlich die
  Noten der Controller-Rolle (`controllerNoteSubToken`);
  `onFeedbackEcho`/`onLearnCompleted` tragen den Address-Kind mit.

## M4b — Aufräumen: zentrale Geräteverwaltung + Feldtest-Fixes (07/2026)

Feldtest-Runde 2 (14.07.2026, Xone:K1): Mapping ging, LED-Feedback blieb
stumm, Toggles verhielten sich momentary, und die drei gleich aussehenden
Geräte-Combos im Grid-Settings-Tab waren unverständlich. User-Richtung:
Geräteverwaltung zentral, Grid-Tab minimal.

- **Kanal ist Geräte-Eigenschaft (LED-Fix):** `RigDevice::midiChannel`
  (1..16, XML-Attribut `midiChannel`, fehlend = 1).
  `ControllerProfile::findBySendAddress (kind, number)` matcht
  KANAL-AGNOSTISCH (die Kanal-Spalten im CSV werden geparst, aber
  ignoriert — das K1 ist frei umkanalisierbar, der User stellt den Kanal
  im MIDI-Menü ein); Feedback wird auf dem Geräte-Kanal gesendet.
  Ursache des stummen Feedbacks: das K1 sendete nicht auf Kanal 1, das
  CSV matchte aber hart darauf — Learn lernt den echten Kanal, deshalb
  ging das Mapping trotzdem (tückisch: halbe Kette funktioniert).
- **Toggle-Flankenerkennung:** `GridPage::applyExternalValue` schaltete
  Toggles direkt (`on = value >= 0.5`) — Note-Off zog sie sofort wieder
  aus (= momentary). Jetzt: Toggles schalten NUR auf steigender Flanke um
  (`externalHigh`-Map pro MacroControlKey), Push bleibt momentary.
  Dazu spiegelt `onFeedbackEcho` jetzt den IST-Zustand des Controls NACH
  dem Anwenden (`currentValueFor`) statt des rohen Eingangswerts — die
  Pad-LED zeigt den Toggle-Zustand, nicht den Tastendruck.
- **Haupt-MIDI-Menü (`MidiRigSettingsComponent`):** Liste gruppiert nach
  Kategorie (Header „Instrumente"/„Controller"; die Kind-Combo pro Zeile
  entfällt), Kanal-Dropdown pro Zeile, Controller-Zeilen mit
  „Grid"-Marker (setzt `gridControllerDeviceId` — ehemals `inputCombo`
  des Grid-Tabs; der erste angelegte Controller übernimmt die Rolle
  automatisch). „+ Gerät" öffnet einen CallOutBox-Anlage-Picker
  (`DeviceCreatePicker`): Profile beider Bibliotheken + „Eigenes …"-
  Einträge; Controller-Auswahl setzt `controllerProfileName` direkt.
- **Grid-Settings-Tab (`GridSettingsView`):** die drei Port-Combos
  (Output/Controller-In/Echo-In) ersetzt durch EIN Instrument-Dropdown
  (RigDevices mit `kind == soundGenerator` → `setGridOutputDeviceId`);
  `ensureGridOutputDevice`/`ensureControllerDevice`/`roleDevicePortName`
  entfallen. Der Tab ist jetzt `MidiRigSettings`-ChangeListener (neue
  Geräte erscheinen live). Alle Rollen-Konsumenten (Hub-Fassaden,
  `refreshRigSubscriptions`, `sendFocusCommand`-Fallback) laufen über
  die Rollen-Ids — keine Änderung nötig.

## M5a — Shift-Ebenen + Chord-Learn (07/2026)

Erster Teil von M5 (User-Entscheidungen 14.07.2026): Die 1:1-Regel
bleibt (ein Hardware-Control → genau eine Zieladresse; Fan-out ist
Sache der Macros), aber eine Adresse darf in MEHREREN Shift-Ebenen
existieren: Pad(s) gedrückt halten + Fader bewegen = eigene Bindung
(`macro1 = fader1`, `macro2 = pad1+fader1`, auch Pad-Akkorde).

- **Adressmodell (`MidiInBindings`):** `Binding` trägt ein kanonisches
  (sortiert, duplikatfrei) `ModifierSet` aus `{channel, note}`-Paaren
  plus `suppressWhileShift`. `bind()` verdrängt nur bei identischer
  Adresse MIT identischem Modifier-Set; eine Note kann nicht ihr
  eigener Modifier sein.
- **Matching:** `MidiInBindings` führt die gehaltenen Noten intern
  (`heldNotes`, gepflegt in `handleIncomingNote`). Pro Eingangs-Event
  feuert genau EINE Bindung: die mit dem größten Modifier-Set, das
  vollständig gehalten ist — exakteste Ebene gewinnt, bei Gleichstand
  die zuletzt gebundene. **Note-Off geht an die per On gewählte Ebene**
  (Latch `noteHeld`), nicht an die aktuell passendste — der User darf
  Modifier vor dem Pad loslassen, ohne dass die Basis-Ebene fälschlich
  die 0 bekommt. Off ohne Latch (Erst-Berührung) fällt auf Best-Match
  zurück (seedet Glättung/Pickup, M4-Muster).
- **Eigenfunktion der Modifier-Pads:** bleibt erhalten (Default).
  Opt-in `suppressWhileShift`: der Press wird aufgeschoben
  (Release-Heuristik) — hat das Pad in diesem Halten als Shift gedient,
  bleibt die Eigenfunktion still; sonst feuert sie beim Loslassen als
  Puls (Press-Wert → 0, mit erzwungenem Pickup, sonst blockierte der
  Soft-Takeover den Sprung 0 → 1).
- **Chord-Learn:** ein CC bindet sofort mit den gerade gehaltenen
  Noten als Shift-Ebene. Nur-Noten-Läufe binden beim Loslassen ALLER
  Noten: die zuletzt gedrückte Note wird Adresse, die beim Drücken
  bereits gehaltenen ihre Modifier (Pad-Akkord-Bindung). Ein Note-On
  bindet also NICHT mehr sofort (M4-Änderung); streunende Offs lösen
  weiterhin nie aus. `onLearnCompleted` trägt das Modifier-Set mit.
- **Persistenz (`GridSessionStore`):** pro Binding zusätzlich
  `modifiers` (String `"ch:note;ch:note"`) + `suppressShift`; fehlend =
  Basis-Ebene/false (rückwärtskompatibel, kein Versionssprung).
- **UI:** MacroPanel zeigt die Shift-Ebene im Tooltip des CC-Felds
  („… + C1, D#1"); Feld-Commits erhalten Modifier-Set + Suppress-Flag.
  Die Zuweisungs-/Übersichts-UI (Map-Tab) folgt in M5b.

## M5b — App-weites Dock + Map-Tab + Overlay (07/2026)

Zweiter Teil von M5 (User-Entscheidung 14.07.2026: „das Tab-Menü auf
allen Seiten einblendbar machen, aber je nach Seite nur die relevanten
Tabs").

- **Dock-Hebung:** `EditorDockPanel` gehört jetzt dem EngineEditor
  (app-weit rechts angedockt, Muster BrowserPanel — `removeFromRight`
  VOR `pageHost.setBounds`). Neue API: `addTab(..., pageMask)`
  (Bitmaske über `TransportBar::PageIndex`, Default `kAllPages`),
  `setActivePage()` (blendet Tab-Buttons um; unsichtbar gewordener
  aktiver Tab springt auf den ersten sichtbaren und feuert
  `onActiveTabChanged`), `removeTab()` (STILL, feuert nie — Aufrufer
  sind Destruktoren). `getPreferredWidth()` liefert 0 auch bei „kein
  Tab auf dieser Page sichtbar". GridPage registriert mpe/cc/macro/
  settings mit Maske „nur Grid-Page" und räumt seine Tabs im Dtor per
  `removeTab` ab (die Contents referenzieren GridPage-Members —
  Lebensdauer-Kopplung statt Member-Reihenfolge-Glück). Die Dock-
  Callbacks (Breite/Persistenz/Tab-Wechsel) verdrahtet der
  EngineEditor; Tab-Wechsel leitet er an
  `GridPage::refreshDockModes()` weiter.
- **Map-Tab (`Source/UI/MappingsListComponent`):** sichtbar auf ALLEN
  Pages (kAllPages) — eine 44-px-Zeile pro MIDI-In-Bindung:
  Control-Name (GridPage synthetisiert über `controlDisplayName`),
  Adresse inkl. Shift-Ebene (`describeBinding`, „CC 20 · Ch 1 + C1"),
  Learn-Kachel (Re-Learn), „Shift"-Kachel (= `suppressWhileShift`,
  nur bei Note-Bindungen) und ×-Löschen. Neu dafür:
  `MidiInBindings::onBindingsChanged` (feuert bei bind/unbind/
  Suppress-Toggle) + `setSuppressWhileShift()`.
- **Map-Overlay (`CcControlLayer::setMapMode`,** Muster `ccMode`):
  Dock offen + Tab „map" → beide Layer (DIY + System) zeigen cyane
  Rahmen mit Adress-Badge um jedes Control und schlucken alle Events
  (kein Spielen); Tap armt MIDI-Learn für das Control (Achse 0),
  das gearmte Control wird orange markiert (`setMapArmedControl`).
  Verlassen des Map-Tabs entschärft ein map-gearmtes Learn.
- **Learn-Besitz verschoben:** `midiInBindings.onLearnCompleted`
  gehört jetzt der GridPage (Overlay + Liste hören mit) und leitet an
  `MacroPanel::handleLearnCompleted()` weiter — das Panel fasst seine
  Felder nur an, wenn das gelernte Control gerade angezeigt wird
  (Map-Learn kann ein anderes Control binden).

## M5c — Conduit-Macro-Ziele mit Modulation (07/2026)

Dritter Teil von M5 (User-Entscheidungen 14.07.2026): der Macro-Tab
bekommt die Zielkategorie „Conduit" — (a) Modul-Parameter des Patches,
(b) Grid-Controls. Semantik: **Modulation statt Übernahme** — der
User-Basiswert bleibt Souverän, wählbar unipolar (+, ab Basis
aufwärts) oder bipolar (±, um die Basis herum), `amount`-skaliert,
grafisch angezeigt.

- **ParamModulationBus (`Source/Core/ParamModulation.h` +
  GraphManager):** Kernentscheidung — der Tree behält den BASISWERT,
  der GraphManager verrechnet `eff = clamp(base + offset·userRange)`
  (Doppel-Clamp User- + Hard-Range) und schreibt den Effektivwert in
  das BESTEHENDE `getParameterTarget()`-Atomic (dokumentierte
  Erweiterung des Dual-State-Musters 6.1, Präzedenz OSC-Fastpath).
  Kein Chassis-Umbau, kein Audio-Thread-Code, uniform für FX- und
  Nicht-FX-Module; das FX-CV-Modell rechnet unverändert obendrauf
  (Macro → CV → Link). Hook in `syncParameterValue` — Fader-Drag
  komponiert live, `addNewNodes` re-appliziert nach Rebuild/Preset
  automatisch. Einträge sind Uuid-keyed und überleben Node-Delete
  BEWUSST (Store dann No-op — Undo eines Deletes reaktiviert die
  Modulation); gelöscht wird über den Target-Dtor
  (`clearParamModulation`). `getParamModulationEffective` rechnet den
  Anzeige-Wert aus Tree-Basis + Offset — ganz ohne Modul-Zugriff.
- **Targets (`Source/Core/ConduitMacroTargets.h/.cpp`):**
  `ConduitParamTarget` (Sink + rootState + persistente nodeUuid/paramId
  — nie ein Modul-Pointer, 5.3; `describe()` löst transient auf, Node
  weg → „fehlt: {Name}"; Dtor cleart, Basis kehrt zurück; Dedupe auf
  dem Offset; toState = nodeUuid/paramId/bipolar/amount/Name-Cache) und
  `GridControlModTarget` (Offset auf einen `MacroControlKey` über
  `IGridControlModSink` = GridPage). Kein Resolver-Lauf nach
  Session-Load nötig (anders als Live-dvid) — die Ids sind selbst
  persistent.
- **Grid-Control-Modulation (GridPage als Sink):** `controlModOffsets`
  pro Key; der Basiswert des Controls bleibt unangetastet — nur
  `feedMacros` (Ausgabe) und die Anzeige verwenden
  `modulatedControlValue`. Re-Entranz-Guard (`controlModFeedGuard`)
  bricht Zyklen A→B→A beim zweiten Besuch desselben Keys.
- **Picker (`Source/UI/ConduitTargetPicker`):** vierte Typ-Kachel
  „CND" im MacroPanel; CallOutBox-Drilldown (Muster
  HardwareTargetPicker) Wurzel = Module (rootState Nodes[], Name =
  moduleId) + Sektion Grid-Controls (GridPage liefert die Einträge
  live); Modul → dsp-Parameter (role "dsp", uiHidden gefiltert).
  Zeile: Zusammenfassungs-Kachel (Tap = Picker) + Polaritäts-Kachel
  (aktiv = ±) + Amount-Feld (0–100 %); Setter wirken live auf
  bestehende Ziele. `rebuildFromBinding` erkennt beide Typen
  (M3-Lektion: fehlende dynamic_cast-Zweige = falscher Live-Zustand).
- **Anzeige:** `CurvedSlider::setModulationValue` (cyaner Zweit-Marker
  am Effektivwert + Verbindungslinie zum Griff, `paintOverChildren`,
  Response-Kurve gratis über `getPositionOfValue`); gefüttert vom
  bestehenden 30-Hz-Meter-Tick des FxModulePanel aus
  `getParamModulationEffective`. `CcControlLayer::modulationValueFor`
  zeichnet den Zweit-Marker an Grid-Controls (Fader: Linie, XY: Ring).
  Generisches `ParameterPanel` (Nicht-FX-Module) zeigt noch keinen
  Marker — die Engine wirkt dort trotzdem (Phase 2).
- **Out of scope:** Audio-rate-Modulation (Macro-Kette ist ~60 Hz
  Message Thread), Marker im generischen ParameterPanel, SysEx (M8),
  Pickup-LED (M6).

## M6 — Pickup-LED + Verhalten (07/2026)

Soft-Takeover-Feedback über Controller-Profile-LEDs. Kernfrage des Users
(„wie universell einbinden") beantwortet durch Trennung in drei Schichten:
WARTE-Zustand pro Eingangs-Adresse (MidiInBindings, headless), ÜBERSETZUNG
in LEDs rein profil-getrieben (`PickupLedRouter`, headless), Hardware-I/O
in GridPage (Muster onFeedbackEcho). Neue Controller bekommen Pickup-LEDs
NUR über CSV-Einträge — kein Code.

User-Entscheidungen (14./15.07.2026): Blinken mit distanz-kodierter Rate
(„wie weit man drehen muss": nah = schnell); Spalten-Status-LEDs auf den
Encoder-Reihe-1-Pushes des K1 (Manual: rot E3–G3 = 52–55, orange E6–G6 =
88–91, grün E9–G9 = 124–127); Detail-Anzeige momentary per Encoder-Druck;
Fader-Pickup ohne Shift auf Pad-Reihe A–D; Takeover-Modus pro Gerät;
K1 hat KEINE Encoder-LED-Ringe (die spekulativen `led_ring`-Einträge aus
M4 sind entfernt).

- **„+ Verhalten"-Bugfix (Ebenen-Wechsel-Sprung):** `SoftTakeover.engaged`
  blieb beim Shift-Ebenen-Wechsel auf der inaktiven Ebene stehen — ihr
  nächstes Event sprang hart (`shouldApply` kurzschließt bei engaged).
  Jetzt: `physicalPositions` (Map `InputAddress{channel, number, isNote}`
  → roher value01) teilt die EINE Fader-Position über alle Ebenen; jedes
  CC-Event disengaged die Geschwister-Ebenen derselben Adresse. NUR CCs:
  ein Note-Toggle muss sein Engagement behalten, sonst verschluckt der
  Takeover den nächsten Press (Velocity vs. Zustand).
- **Warte-Definition (`MidiInBindings::updatePickupStates`, am ENDE von
  `tick()`):** waiting := aktive Ebene (Best-Match bei gehaltenen
  Modifiern) nicht engaged UND Position BEKANNT UND Distanz >
  `kPickupEpsilon` (= `shouldApply`-Epsilon, eine Konstante — LED und
  Gate divergieren nie). Unbekannte Position wartet NIE (kein
  Weihnachtsbaum nach App-Start; Blink = belegter Konflikt — deckt
  zugleich die User-Regel „Anzeige erst bei leichter Bewegung"). Neue
  Naht `onPickupStateChanged (InputAddress, PickupState{waiting,
  distance01, modifiers, activeRecently})` mit Transition-Dedupe;
  `setPickupEnabled(false)` = Sprung-Modus (Gate übersprungen, nie
  waiting; Rückschalten disengaged alle). Noten warten nie (momentary).
- **`PickupLedRouter` (Source/Core, headless):** rechnet pro Tick den
  LED-SOLL-Zustand und difft gegen den letzten Send (Dedupe). Vier
  Mechanismen: (1) Gruppen-Status — CSV-Feld `group` bündelt Controls,
  das Control mit `status_red`/`status_amber`/`status_green`-Feedback ist
  die Status-LED: rot = Fader der Gruppe wartet (dominiert), orange =
  nur Knobs, grün = nichts wartet + ≥ 1 Member gebunden, aus = ungebunden;
  aktiv gedrehtes wartendes Control lässt die Farbe distanz-kodiert
  blinken (Halbperiode 20→4 Ticks). (2) Detail-Modus momentary: Push der
  Status-Note gehalten → `led_pickup`-Adressen der Member zeigen
  Einzel-Status (grün-Ebene solid = abgeholt, Basis rot-blinkend =
  wartet, aus = ungebunden; grüne Ebene via `led_layer_c` des Ziel-Pads
  aufgelöst). (3) Einfache Profile: `led_pickup` OHNE Gruppe blinkt
  immer bei waiting. (4) Shift-Pad-Anzeige (profil-unabhängig, **M6.1
  geändert 15.07.2026**): solange ein Modifier-Pad gehalten wird und die
  physische Position der Shift-Ebene bekannt ist, zeigt das Pad die
  RICHTUNG **solid** (rot=`led`=verringern, orange=`led_layer_b`=erhöhen,
  grün=`led_layer_c`=gefunden; kein Blinken, kein `activeRecently`-Gate —
  Status steht „im Moment des Drückens"). Den Näherungswert (Blink) trägt
  weiterhin die Spalten-Status-LED aus Mechanismus 1. Dafür meldet
  `PickupState` jetzt `physicalAbove` (Vorzeichen `physisch−Software`) und
  `aligned` (abgeholt/engaged, NUR für Shift-Ebenen mit Modifiern) —
  aligned-Einträge landen im Router-`waiting`-Store, `waitingFor()` filtert
  sie für Mechanismus 1–3 aus. **Feldtest-Fix (15.07.2026):** rot/orange
  flackerten nur kurz und wurden dann dunkel — Ursache war der Echo-Pfad
  (`onFeedbackEcho` sendet für ein Pad ALLE Farb-LEDs), der die Router-Farbe
  überschrieb, während der Router-Dedup (eigenes `lastSent`) die Korrektur
  verhinderte. Fix: `onFeedbackEcho` überspringt LEDs mit
  `PickupLedRouter::isManaging(...)` (hält aber den Echo-Cache aktuell für den
  späteren Restore).
  LED-Restore beim Verlassen jedes Zustands aus dem Echo-Cache
  (`GridPage::lastFeedbackSent`) — bloßes 0-Senden würde Toggle-LEDs
  löschen. Der Echo-Pfad überspringt `led_pickup` und `status_...`
  (exklusiv Router). Status-Pushes bleiben mapp-/lernbar (Router
  beobachtet passiv).
- **TakeoverMode pro Gerät:** `RigDevice::takeoverMode` (pickup|jump,
  XML fehlend = pickup), fünfte Combo „Pickup"/„Sprung" pro
  Controller-Zeile im MIDI-Menü. GridPage überträgt Modus + Profil in
  `refreshRigSubscriptions()` (Setter idempotent — läuft bei JEDEM
  Registry-Broadcast); Geräte-WECHSEL der Controller-Rolle resettet den
  Router (Restores liefen sonst ans neue Gerät).
- **Session-Load-Invalidierung gratis:** Takeover wird nicht persistiert,
  `bind()` startet disengaged; `physicalPositions` überlebt als Member —
  Session-Wechsel zur Laufzeit blinkt bei belegtem Mismatch sofort.
- **K1-CSV:** 4 Status-Controls `enc_r1_n_push` (send Note 52–55, drei
  Status-Farben, `group=col1..4`); Member `enc_r1/r2/r3/r4/fader{n}` mit
  `group=col{n}` und `led_pickup`-Detail-Pads: r1 → Taster 48–51, r2 →
  Pad-Grid E–H (32–35, kein Taster unter Reihe 2), r3 → Taster 44–47,
  r4 → Taster 40–43, Fader → Pad-Grid A–D (36–39, expliziter
  User-Wunsch). **Zuordnung r1/r2/r3/r4 ist Layout-Vorschlag — Feldtest
  bestätigt.** Encoder-Pushes sind damit erstmals reguläre Profil-Controls.
- **Dokumentierte Grenze:** Echo und Blink können auf geteilten Pad-LEDs
  kurz konkurrieren (Restore heilt) — Echo-Suppression wäre Nachschlag.
  Blink-Konstanten hängen an der 60-Hz-Hub-Tick-Annahme.

## M7 — Channelstrip-Ebenen (Top-Encoder) (07/2026)

User-Idee (15.07.2026): die 4 K1-Top-Encoder (CC0–3) drehen statt zu senden
je eine von **3 Binding-Bänken** für alle übrigen Controls IHRER Spalte
(Channelstrip) — verdreifacht die Belegung von 3 Knobs + Fader + 7 Pads pro
Spalte. Spalte = Profil-`group` (col1–4); layer-neutral bleiben nur die
Setup-Knobs (CC20/21) und die Shift/Setup-Reihe.

User-Entscheidungen (15.07.2026): Belegung über „**aktive Ebene =
Lernziel**" (Encoder auf die Ebene drehen, dann Control per MIDI-Learn — kein
neues UI, spiegelt M5a); Ebene **pro Session** persistiert; Ebenen-Farben
**1 rot / 2 grün / 3 orange**; beim Drehen blinken alle Pads der Spalte kurz
in der Ebenen-Farbe; Encoder dauerhaft Layer-Selektoren (keine Macro-Knobs
mehr auf CC0–3). Zuordnung Encoder→Spalte = ganzer vertikaler Channelstrip
(User-Chart `xone K1 midi.png`), NICHT horizontale Reihe.

- **`Source/Core/ChannelStripLayers.h/.cpp` (headless, testbar):** Schritt-
  Akkumulator pro Spalte, `kStepsPerLayer=8` Detents je Zone, 3 volle Zonen
  (`kMaxPos=23`), an den Enden geklemmt (kein Wrap 3→1). Endlos-Encoder
  signed dekodiert (`decodeSignedDelta`: 1..63=+, 65..127=−, 0/64=0) —
  isoliert, weil geräteabhängig (Feldtest kann die Kodierung hier justieren).
  `feed()`→{layer, layerChanged}, `setLayer`/`snapshot` für Persistenz.
- **`MidiInBindings` (Ebenen-Dimension):** `Binding` trägt `column`
  (Profil-`group`, leer = nicht geebent → immer aktiv) + `layer`. `bind()`
  löst bei `column==kAutoColumn` (Live/Learn) Spalte über den `columnResolver`
  (Besitzer GridPage, profil-getrieben) + die aktuell aktive Ebene auf; der
  Persistenz-Load übergibt explizite Werte. **Dedup schließt column+layer ein**
  (Bänke derselben Adresse koexistieren, analog M5-Shift-Ebenen).
  `bestMatch` filtert eine geebente Bindung aus, wenn `layer !=
  activeLayer(column)`. `setActiveLayer`/`activeLayer` je Spalte.
- **`PickupLedRouter` (Basis-Mechanismus, M7b geändert):** `setColumnLayer`
  hält die aktive Ebene je Spalte DAUERHAFT — alle `type=="pad"`-Controls der
  Spalte leuchten solid in der Ebenen-Farbe (0 `led`=rot, 1 `led_layer_c`=grün,
  2 `led_layer_b`=orange) als BASIS (erster Block in `computeDesired`, die
  Pickup-/Detail-/Shift-Mechanismen 1–4 überschreiben sie als momentane
  Overrides). Aktives Pad (Echo-Wert > 0 = Toggle/Button an) blinkt im
  8tel-Takt, ein Ebenen-Wechsel lässt die Spalte `kLayerChangeTicks` lang im
  16tel-Takt flackern — beide **tempo-synchron** über `setBeatPosition`
  (LinkClock.getBeatPosition, GridPage füttert je Tick; `beatBlinkOn` = an in
  der ersten Hälfte jeder Unterteilung). **User-Entscheidung 15.07.2026:**
  dauerhafte Layer-Farbe statt transientem Blink.
- **`ControllerProfile.role` (+ Parser-Spalte `role`):** `role=layer_select`
  markiert den Spalten-Selektor. Neuer Controller bekommt Ebenen NUR über CSV
  (Rule midirig: kein Code). K1-CSV: `enc_r1_1..4` `role=layer_select` (kein
  led_pickup mehr), alle Pads mit `group=col1..4` (Spaltenzuordnung aus dem
  User-Chart), Status-Pushes/Setup layer-neutral.
- **GridPage:** `channelStripLayers`-Member; `refreshRigSubscriptions` baut den
  `layerSelectCcToColumn`-Cache aus dem aktiven Profil (kein Profil-Scan je
  Event) und spiegelt die persistierte Ebene in Bindungen UND Router-Basis
  (`setColumnLayer`); `routeLayerSelectCc` fängt Selektor-CCs VOR den Bindungen
  ab (`feed` → `setActiveLayer` + `setColumnLayer`). `columnResolver` liefert
  die `group` NUR für „normale" Spalten-Controls (nicht Selektor, nicht
  Status-Push). LinkClock durchgereicht (Ctor-Param) für `setBeatPosition`.
- **Persistenz (`GridSessionStore`):** pro Binding `column`/`stripLayer`
  (fehlend = ungeebent, rückwärtskompatibel) + `StripLayer`-Kinder
  (column→aktive Ebene). **Property heißt `stripLayer`, NICHT `layer`** —
  `keyToState` belegt `layer` bereits für die Macro-Art des Keys (Kollision =
  falscher Key beim Laden, s. Lektionen).

**Grenzen/offen:** K1-Encoder-Relativkodierung ist eine Annahme (Feldtest);
Knob-/Fader-`led_pickup` teilen sich physische LEDs mit den geebenten Pads
(K1-Eigenheit, M6-Grenze); kein On-Screen-Ebenen-Indikator (Hardware-LEDs sind
das Feedback). **Reconciliation M6.1↔M7b:** die dauerhafte Layer-Farbe ist die
BASIS, die momentanen Pickup-/Detail-/Shift-Anzeigen (inkl. der M6.1-Richtung
auf gehaltenen Modifier-Pads) überschreiben sie, solange sie greifen —
NICHT die vom User genannte „Shift-Pad = immer 8tel Layer-Farbe"-Variante
(offen: würde die Richtungs-Hilfe ersetzen, bewusst zunächst als Override
belassen). **Feldtest offen.**

## M8 — Bidirektionale Ribbons/Motorfader (07/2026)

Anlass: User-Fund Frontier **AlphaTrack** (Motorfader + Touch-Strip +
3 Touch-Encoder, USB). Protokoll-Quelle: offizielle Doku
„AlphaTrack Native Mode MIDI Interface Description v1.0" (frontierdesign.com,
Kopie gesichtet 16.07.2026): Fader = **Pitch Bend ch1 in BEIDE Richtungen**
(Motor faehrt auf zurueckgesendetes `e0 yy zz`, 10-bit), Fader-Touch =
Note 0x68; Strip = Pitch Bend **ch10** (absolute Position 0..31, `nn<<7`),
Touch-Noten 0x74/0x7b; Encoder = CC 16..18 **relativ** (1..63 = +,
65..127 = − — exakt die `decodeSignedDelta`-Kodierung aus M7); Buttons =
Noten, LEDs teilen die Notennummer (Echo-Pfad ab Werk kompatibel); LCD =
SysEx (**M9**, ebenso das Native-Mode-Force-SysEx `f0 00 01 40 20 01 00 f7`
— bis dahin Native Mode im AlphaTrack-Treiber-Applet einstellen).

User-Entscheidungen (16.07.2026): Motor spiegelt **kontinuierlich,
touch-gated** (Finger gewinnt; Loslassen faehrt nach); Motor zeigt den
**Basiswert**, nie den M5c-Effektivwert (kein Motor-Tanzen/Verschleiss);
Motor folgt der **aktiven Ebene** (Shift-/Channelstrip-Wechsel = Sprung auf
den Wert der neuen Bank); Ribbon arbeitet **relativ/Scrub** (voller Strip =
voller Regelweg, Neuaufsetzen ankert ohne Sprung).

- **Transport:** `ControllerEvent::Kind::pitchBend` (14-bit, `is14Bit`,
  number = 0 — die Adresse IST der Kanal); MidiPortHub reicht
  `isPitchWheel()` durch (Latest-Pending-Packing traegt genau 4 Kinds).
- **Bindungs-Adressraum:** PB-Bindungen leben als Nummer
  `128 + Kanal` (129..144, `grid::pitchBendBindingNumber`) im CC-Namensraum —
  Persistenz unveraendert (alte Sessions kennen keine Nummer > 127), zwei
  PB-Kanaele (Fader ch1 / Strip ch10) kollidieren nie im kanal-agnostischen
  Matching. Learn bindet PB wie CCs (`handleIncomingPitchBend`).
  Map-Tab/MacroPanel zeigen „Pitch Bend" statt „CC 129"; das
  `midiInCcField` klemmt bis 144 (sonst korrumpierte ein Feld-Commit die
  Bindung auf CC 127).
- **Adress-Modi (`MidiInBindings::AddressMode`,** profil-getrieben ueber
  `setAddressMode`, Besitzer GridPage/`rebuildAddressModes`): `absolute`
  (Default, Takeover wie gehabt) · `direct` (Motorfader: Werte greifen
  sofort, Adresse wartet NIE — Pickup-Exemption auch in
  `updatePickupStates`) · `scrub` (absoluter Positions-Strom relativ:
  Delta aufeinanderfolgender Positionen, Anker nach Pause >
  `kScrubGapTicks` ≈ 250 ms) · `relativeTicks` (Encoder-Ticks, Kodierung
  aus dem Profil — s. M8.1 —, Skala CSV-Spalte `steps`, Default 127).
  Deltas akkumulieren in `Binding::pendingDelta01` und
  werden im tick() DIREKT (ohne Glaettung/Takeover) auf
  `clamp(current + delta)` angewendet — sie folgen `bestMatch`, treffen
  also die aktive Shift-/Channelstrip-Ebene.
- **`PositionFeedbackRouter` (Source/Core, headless):** wert-getriebenes
  Gegenstueck zum Echo-Pfad — pro Tick den Basiswert der AKTIVEN Bindung
  jedes `position`-Feedback-Controls diffen (Dedupe 14-bit) und senden;
  Touch-Gate ueber die Touch-Noten. Seams (GridPage): `send`
  (pitchBend → `MidiMessage::pitchWheel(feedback.channel, value14)` — der
  PB-Kanal kommt aus dem CSV, NICHT vom Geraete-Kanal; cc/note → Geraete-
  Kanal, value14 >> 7) und `currentBoundValueFor`
  (= `activeBindingForAddress` + `controlValueFor`). Der Echo-Pfad
  ueberspringt `position`-meanings UND pitchBend-Feedback komplett
  (exklusiv Router — ein Event-Echo wuerde gegen das Touch-Gate senden).
- **CSV-Schema v1 erweitert:** `*_kind` kennt `pitchbend`/`pb`
  (send_number darf dann fehlen → 0; `findBySendAddress` matcht PB ueber
  den KANAL); neue optionale Spalten `mode` (leer/`scrub`/`relative`),
  `steps`, `touch_number`; neuer `type=touch` fuer reine Touch-Sensoren
  ohne Eigenfunktion (Send-Note wird nur gefiltert).
- **Touch-Noten-Filter (Learn-Falle):** GridPage leitet Touch-Noten
  (`positionRouter.isTouchNote`) NUR an den Router — sonst wuerde der
  Griff zum Fader im MIDI-Learn die Touch-Note statt des Fader-PB binden
  (AlphaTrack sendet Touch VOR der ersten Bewegung).
- **Factory-CSV `Assets/ControllerProfiles/Frontier_AlphaTrack.csv`:**
  32 Controls aus der Native-Mode-Doku (Fader mit position-Feedback +
  Touch 104; Strip scrub + Touch 116 + type=touch 123; 3 Encoder relativ
  mit Touch 120..122 + Pushes 32..34; 22 Buttons inkl. LED-Echo, wo die
  Hardware eine LED hat). Output-only-LEDs ohne Send-Adresse (ANY-Solo
  0x73, AUTO WRITE/READ 0x4b/0x4e) sind bewusst nicht abgebildet (kein
  Konsument). Library referenziert das BinaryData-Symbol direkt
  (M4-Lektion `originalFilenames`).

## M8.1 — Relativ-Kodierung wird profil-getrieben (Feldtest 16./17.07.2026)

**Feldtest AlphaTrack bestanden** („alles funktioniert perfekt") mit EINEM
Fund: die Encoder erhoehten Werte korrekt, sprangen beim Zurueckdrehen aber
sofort auf 0. Ursache: M7s `decodeSignedDelta` liest ZWEIERKOMPLEMENT
(1..63 = +, 65..127 = Wert−128), das AlphaTrack kodiert aber
SIGN-MAGNITUDE (Bit 6 = Vorzeichen, Bits 0..5 = Betrag). „1 Tick zurueck"
(0x41) wurde damit zu **−63** — der Wert fiel durch. Positive Ticks sind in
beiden Verfahren identisch, deshalb fiel es beim Hochdrehen nicht auf.
Die Native-Mode-Doku belegt Sign-Magnitude durch ihr eigenes Beispiel:
`0x43` = „3 Ticks gegen den Uhrzeigersinn" (Zweierkomplement waere −61).

- **`Source/Core/RelativeEncoding.h` (neu, header-only, pur):**
  `RelativeEncoding{twosComplement, signBit, binaryOffset}` +
  `decodeRelativeDelta()` + `parseRelativeEncoding()`. Dieselbe Dreiteilung
  wie Abletons Remote-Scripts („2's Comp." / „Signed Bit" / „Bin Offset") —
  mehr Verfahren gibt es in der Praxis nicht.
- **CSV-Spalte `rel_encoding`** (`signbit`/`sign`, `binoffset`/`bin`, leer =
  `twosComplement`): neue Encoder-Geraete bekommen ihre Kodierung damit rein
  ueber Daten, nie ueber Gerätecode (Rule midirig). Profile ohne die Spalte
  verhalten sich unveraendert (K1 unberuehrt).
- **Reichweite:** `ControllerControl::relEncoding` → sowohl
  `MidiInBindings::setAddressMode(..., relEncoding)` (Bindungen) ALS AUCH
  `ChannelStripLayers::feed(..., encoding)` (M7-Ebenen-Selektoren, ueber
  `GridPage::LayerSelectEntry`) — sonst haette ein Sign-Magnitude-Geraet
  beim Ebenen-Waehlen denselben Sprung. `decodeSignedDelta` bleibt als
  duenner Zweierkomplement-Wrapper (M7-Aufrufer + Tests).

**Grenzen/offen:** LCD + Native-Mode-Force-SysEx = M9; Strip sendet nur
32 Stufen (Scrub glaettet das inhaerent); ein `position`-Feedback als
CC ist vorgesehen, aber ungetestet an echter Hardware (kein Motor-CC-
Geraet im Rig); Encoder-`steps=127` ist der Startwert (Feinjustage nach
Gefuehl offen).

**Hardware-Voraussetzung (Dev-PC):** der AlphaTrack-Treiber (2009) laeuft
unter Windows 11 nur mit lokal signiertem Katalog + Testsigning — Skripte
und Doku unter `C:\Users\leonn\AlphaTrack-signed` (nicht Teil des Repos).

## Live-Remote-Bridge (Verweis, 17.07.2026)

Der AlphaTrack dient zusaetzlich als Ableton-Live-Fernbedienung — die
Bridge lebt fachlich im TouchLive-Subsystem: **docs/TouchLive.md §10l**
(`Source/TouchLive/LiveRemoteBridge` + `AlphaTrackLcd`). MIDI-Rig-seitige
Bausteine:

- **Rolle `MidiRigSettings::liveRemoteDeviceId`** („Live"-Marker im
  MIDI-Menue, abschaltbar). KONFLIKTREGEL: Grid- UND Live-Rolle auf
  demselben Geraet → Bridge inaktiv.
- **CSV-Control-ID-KONVENTION** (Bridge-Rollen-Aufloesung, statt neuer
  `role`-Werte): `fader` (Pitch-Bend + `touch_number`), `pan`, `f1..f4`,
  `shift`, `track_l`, `track_r`, `mute`, `solo`, `rec_arm` — Controller,
  die die Bridge bedienen sollen, muessen diese IDs tragen.
- **Profil-Spalte `display`** (erste nicht-leere Zelle gewinnt, Muster
  `device`): Display-Faehigkeit des Geraets; bekannter Wert
  `alphatrack_lcd` (2x16 via SysEx). SysEx bleibt Sende-only (E6);
  das Native-Mode-Force-SysEx sendet die Bridge bei jedem Resolve
  (Treiber-Applet ueberfluessig).

## Lektionen

- **Relativ-Encoder: positive Richtung beweist NICHTS** (M8.1, Feldtest).
  Zweierkomplement und Sign-Magnitude sind fuer Vorwaerts-Ticks
  BITGLEICH — ein Encoder-Test, der nur hochdreht, laeuft in beiden
  Kodierungen sauber und bestaetigt die falsche Annahme. Erst das
  Zurueckdrehen trennt sie (und dann drastisch: „1 Tick zurueck" wird zu
  −63). Encoder IMMER in beide Richtungen testen; Kodierung nie raten,
  sondern am Doku-BEISPIEL verifizieren (die Bereichsangabe „backward
  41–7f" allein ist mehrdeutig — erst „0x43 = 3 Ticks CCW" entscheidet).

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
- **ValueTree-Property-Namen kollidieren still** (M7 einmal bezahlt): der
  Binding-Serialisierer schreibt zuerst `keyToState` (Properties `layer`
  (Macro-Art!), `controlId`, `axis`), dann eigene Felder. Eine neue Property
  `layer` für die Channelstrip-Ebene ÜBERSCHRIEB die Key-Art → beim Laden
  entstand ein falscher Key (Bindung „verschwand"). Symptom: Roundtrip-Test
  `bindingFor(...) == nullptr` trotz korrektem `count()`. Neue Binding-
  Properties immer gegen die von `keyToState` gesetzten Namen prüfen; hier
  `stripLayer` statt `layer`.
- **Glob-Muster gehören NIE in C-Blockkommentare** (beide Richtungen,
  M2 + M4b je einmal bezahlt): `**/*.csv` — das `*/` BEENDET den
  Kommentar (Kaskadenfehler); `Ordner/*.csv` — das `/*` triggert Clangs
  `-Werror,-Wcomment` (MSVC schweigt, fällt erst in der CI auf).
  Pfad-Konventionen in Kommentaren immer umschreiben („alle .csv im
  Ordner X, rekursiv/flach") statt als Glob zu notieren; in
  `//`-Zeilenkommentaren ist `/*` unkritisch.
- **`BinaryData::originalFilenames` trägt KEINEN Ordnerpfad** — nur den
  reinen Dateinamen, egal aus welchem `Assets/`-Unterordner die Datei via
  `juce_add_binary_data` eingebunden wurde. Ein generischer Scan über
  „alle `.csv`-Ressourcen" (Muster `MidiProfileLibrary`) kann deshalb
  NICHT zwischen z. B. Klangerzeuger- und Controller-Profilen unterscheiden,
  sobald beide Kategorien im selben `ConduitAssets`-Blob landen. Bei
  wenigen, namentlich bekannten Factory-Dateien (M4: nur der Xone:K1)
  stattdessen das generierte Symbol direkt referenzieren
  (`BinaryData::AllenHeath_XoneK1_csv`/`_csvSize`) statt zu scannen.
- **`\x`-Hex-Escapes in C++-String-Literalen sind NICHT auf 2 Ziffern
  begrenzt** (bereits in M3 gefunden, hier erneut bestätigt) — vor jedem
  neuen UTF-8-Literal prüfen, ob auf das letzte `\xNN` ein Hex-Digit-
  Zeichen (0-9a-fA-F) im Klartext folgt; im Zweifel mit Python
  (`s.encode('utf-8')`) verifizieren statt von Hand zu rechnen.

## Meilensteinleiter

  M1  MidiPortHub + Registry — M1a Registry+Matching (erledigt 07/2026) + M1b Hub-Kern: Portbetrieb/Queues/Drain/Ablösung der Ein-Port-Klassen + Migration + Settings-UI (erledigt 07/2026)
  M2  Profile + NRPN + PC — midi.guide-CSV-Parser (Klangerzeuger-Profile), NRPN-Assembler pro Port, Program-Change Senden/Empfangen — erledigt 07/2026 (inkl. Hardware-Picker-Vorgriff: NRPN/CC-Ziele aus Profilen)
  M3  Semantischer Picker — Geräte-/Parameter-Auswahl-UI (Analogie Ableton-Parameter-Browser Track→Device→Parameter) — erledigt 07/2026
  M4  Controller-Profile + LED — Conduit-Controller-Profile-v1-CSV-Schema, Send-Adresse + bis zu 3 Feedback-Adressen pro Control — erledigt 07/2026
  M5  Map-Modus + Tab + Chord-Learn — M5a Shift-Ebenen/Chord-Learn · M5b app-weites Dock + Map-Tab + Overlay · M5c Conduit-Macro-Ziele mit Modulation — komplett erledigt 07/2026
  M6  Pickup-LED + Verhalten — Soft-Takeover-Feedback über Controller-Profile-LEDs (PickupLedRouter: Spalten-Status/Detail-Modus/Shift-Pad-Anzeige, TakeoverMode pro Gerät, Ebenen-Wechsel-Sprung-Fix) — erledigt 07/2026 (Feldtest offen); M6.1 (15.07.2026): Shift-Pad zeigt die RICHTUNG solid (rot/orange/grün, kein Blinken) statt zu blinken — Näherungswert bleibt die Spalten-Status-LED
  M7  Channelstrip-Ebenen — Top-Encoder (role=layer_select) wählen pro Spalte eine von 3 Binding-Bänken (ChannelStripLayers, 8-Step-Zonen, Ebenen-Blink, „aktive Ebene = Lernziel", pro Session persistiert) — erledigt 07/2026 (Feldtest offen)
  M8  Bidirektional Ribbons — Motorfader-/Ribbon-Feedback in beide Richtungen (PitchBend-Adressen 128+Kanal, AddressModes direct/scrub/relativeTicks, PositionFeedbackRouter, AlphaTrack-Factory-CSV) — erledigt 07/2026, **Feldtest AlphaTrack bestanden** (17.07.2026); M8.1: Relativ-Kodierung profil-getrieben (`rel_encoding`, RelativeEncoding.h — AlphaTrack ist sign-magnitude, nicht Zweierkomplement)
  M9  SysEx-Snippets — Sende-only Hex-Snippets mit optionalem `{v}`-Platzhalterbyte + AlphaTrack-LCD/Native-Mode-Force — offen

## Referenzen

- ADR 006 (Entscheidungen E1–E7: Profile-Formate, Registry, Transport,
  Threading).
- Rule `midirig` (.claude/rules/midirig.md) — mechanische Invarianten,
  lädt bei Arbeit an `Source/Core/Midi*` / `Source/Core/HardwareCcDatabase*`.
- docs/Grid.md Block G/H4/L2 — bestehender Code-Vorläufer
  (MidiControlInput, MidiNoteInput, MidiInBindings, HardwareCcDatabase),
  bis zur Migration weiterhin dort beheimatet.
