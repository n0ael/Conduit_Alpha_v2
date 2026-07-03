# Conduit Alpha — Projektstatus

> Letzte Aktualisierung: 2026-07-03 | wird nach jedem Meilenstein gepflegt
> Architektur-Referenz: [CLAUDE.md](CLAUDE.md) | Repo: n0ael/Conduit_Alpha_v2

## Fundament (steht komplett)

- **Engine:** JUCE-8-Standalone-App, `AudioProcessorGraph` als DSP-Engine, ValueTree als Single Source of Truth
- **Graph-Swap:** glitch-frei mit Fade-Out/Fade-In-Zyklen, Batch-Coalescing (Undo/Preset-Load/Bulk-Delete), zweiphasiges Delete (Zombie-UI-Schutz)
- **Undo/Redo:** alle patchbaren Aktionen über `UndoManager`, inkl. undo-fähigem `renameNode`
- **Preset-System:** Save/Load mit `isDirty`-Guard, undo-fähiges Laden (CLAUDE.md 5.4)
- **OSC:** produktiv auf Port 9000 (end-to-end per UDP verifiziert), eindeutige user-editierbare `named_ids` (factoryId vs. moduleId getrennt), Dual-State-Pfad (SPSC-Queue → Audio Thread, async → ValueTree)
- **Clock/Link:** Ableton Link integriert, `IClockSource`/`IClockSlave`, LinkClock, beat-synchroner LFO, Transport-UI mit Tempo + Peer-Status
- **Scope-Modul:** lock-free Ringbuffer (min/max-Bins), 30-fps-Waveform, Audio-Fallback
- **CI:** GitHub Actions (Ubuntu) mit TSan + ASan bei jedem Push auf master; lokal ASan via MSVC-Preset

## Aktueller Meilenstein (Juli 2026 — in Arbeit)

**Airwindows-Massen-Port: 54 neue FX-Module (alle Airwindows-Consolidated-Favoriten des Users):**

- **Ausgangslage:** User hat in Ableton Airwindows Consolidated durchgeschaut und 53
  Plugins als Favoriten markiert (Screenshot der Favoritenliste); Auftrag: alle als
  eigenständige Conduit-Module portieren, autonom über Nacht, lokale Commits ohne Push.
- **Umsetzung:** 6 parallele Batch-Agenten (je 8 Plugins) für die "einfachen" Effekte
  (EQ/Dynamics/Lo-Fi/Sättigung/Effects) + 2 parallele Agenten für die 4 größten
  Reverbs (VerbTiny/kWoodRoom, kBeyond/kCathedral5) + Chamber/Galactic selbst portiert
  (RT-Safety der Reverb-Delaybuffer vorab geprüft: alle bereits im Original fest
  dimensionierte C-Arrays, kein `new`/`malloc` im Verarbeitungspfad nötig). Quelle:
  `plugins/LinuxVST/src/<Name>/` (github.com/airwindows/airwindows, MIT), per `curl`
  verifiziert. Muster: `AirwindowsProcessorModule` (bestehender generischer Chassis-
  Wrapper) + je Plugin ein dünner `Airwindows<Name>Module` (~10 Zeilen) + Eintrag in
  `AirwindowsRegistry`/`ModuleFactory`/Browser/CMakeLists.
- **Zentrale Integration:** Registry/CMakeLists (2 Ebenen)/ModuleFactory/EngineEditor-
  Browser per Skript verdrahtet (54× Include+Eintrag je Datei), dünne Wrapper-Module
  generiert, ein neuer generischer Registry-Sweep-Test (`AirwindowsModuleTests.cpp`)
  ersetzt 54× Copy-Paste-Testboilerplate (iteriert `getRegisteredPlugins()`, wrappt
  jeden Eintrag chassis-konform, sweept alle DSP-Parameter NaN/Inf-frei).
- **Gefundene und dokumentierte Abweichungen vom 1:1-Port** (`PORTING_NOTES.md`):
  Off-by-one-Array-Fix (FatEQ/Isolator3/Pop2/Silken, Original-Bug, geclampter Index
  erreichte nie den Rand außerhalb Conduit-Zielraten), UB-Fix bei `derez==0` (kBeyond,
  `(int)+inf`-Konvertierung), `rand()`-im-DSP-Pfad-Fix (TapeDust, echter CLAUDE.md-
  3.1-Verstoß im Original, durch fpd-Xorshift ersetzt), zwei bewusst NICHT reparierte
  Original-Eigenheiten (kCathedral5 Kanaltausch, kWoodRoom Doppel-Increment — beide
  identisch in beiden Original-Funktionen verifiziert, also echtes Original-Verhalten).
- **Live-Test-Fund (User, GlitchShifter): Knacksen bei Tighten/Note/Trim → Kern
  bewusst umgebaut (User-Freigabe „komplett offen für tiefgreifende Änderungen"):**
  Vier punktuelle Fixes (Registry-Reset entfernt, Position umskaliert, gcount-
  Modulo-Wrap statt Hart-Reset, 16-Sample-Declick) reduzierten das Knacksen nur.
  Diagnose: (a) jeder Splice ist im Original ein harter Lese-Sprung mit Ein-Sample-
  Blend — knackst zunehmend mit Note/Trim-Auslenkung; (b) Tighten ändert die Ring-
  Geometrie bei offenem Ausgang — prinzipbedingt nicht klickfrei flickbar. Umbau
  (Original-Splice-AUSWAHL per Zero-Cross-Matching unverändert): **Dual-Tap-
  Crossfade** (zwei Lese-Taps, Splice-Trigger mit Vorlauf, alter Tap spielt beim
  Überblenden weiter, xfade-Inversion hält Swaps stetig, Fade `clamp(width/2,16,512)`)
  + **geduckter Geometrie-Wechsel** (Wet ~1,3 ms auf 0 → width/gcount/Taps tauschen
  → ~5 ms wieder hoch; aus Klicks werden kurze Wet-Dips). Bei neutralem Note/Trim
  entstehen keine Splices mehr (klickfrei per Konstruktion). Doku: PORTING_NOTES.md
  + Header-Kommentar GlitchShifter.h.
- **GlitchShifter-Feinschliff über WAV-Klick-Analyse (messbasiert statt hörbasiert):**
  User nahm Conduit-Captures auf (Capture-Tap!), ein Node-Detektor-Skript fand
  Sample-Diskontinuitäten mit Zeitstempel/Kontext, ein neuer In-Test-Klick-Audit
  (Sinus + automatisierte Regler-Sweeps + Debug-Getter-Zustandslog) machte die
  Ursachen reproduzierbar: mid-fade Tap-Teleports (Fix: Splice-Gate auf
  abgeschlossenen Crossfade), Kernel-Vorauslesen am Schreibkopf (Fix:
  3-Sample-Korridor), nie mehr beschriebener Slot 0 (Fix: Original-Wrap),
  Epochen-Narben nach Geometrie-Wechseln (Fix: Taps auf frische Position +
  Registry-Reset im stummen Duck + Duck-Hold) und Feedback-DC-Lock durch
  unbegrenzte Extrapolation (Fix: Clamp auf ±24-Bit-Skala). Messwerte:
  Capture 1 = 669 Klick-Events (Sprünge bis 0.40), Capture 2 nach Fixes =
  53 Events (großteils Synth-Attacks des Testmaterials, Rest ≤ 0.098), Audit
  intern 0.26 → 0.048 (= inhärente Kernel-Textur des Originals). **User-Abnahme:
  „gut genug", Klick-Audit + Feedback-Regression bleiben als Dauertests.**
- **Verifikation:** Hauptsuite (`ConduitTests`) Debug + ASan grün (286 Testfälle /
  11855 Assertions, inkl. generischem Registry-Sweep über alle 57 Airwindows-Module).
  DSP-Level-DoD-Suite (`ConduitAirwindowsTests`, separates Target) läuft — Ergebnis
  wird nach Abschluss hier nachgetragen. App (Debug) gebaut und manuell getestet.
- **Abschluss:** User-Abnahme erteilt, Commit + Push auf master (User-Freigabe
  03.07.2026 nachmittags — ersetzt die nächtliche „nur lokal"-Vorgabe).
- **CI-Nachsorge (Lehrstück):** Die CI war schon VOR dieser Session rot (seit
  AirwindowsModuleTests-Einführung) — Clang hatte die kompletten FX-Chassis-
  Dateien M1–M7 nie kompiliert. Nachgeschobene Fixes: fehlender `static`
  (missing-prototypes), 5× Float-== → `juce::exactlyEqual` (M1-Altbestand),
  Lambda-Shadowing im FxModulePanel (M6b-Altbestand). Zusätzlich: das
  40-min-CI-Limit fiel durch den 5^6-Vollkreuz-Sweep der 6-Parameter-Reverbs
  unter TSan → Sweep gedeckelt (Vollkreuz ≤ 4 Parameter, darüber 625
  LCG-gesampelte Kombos; Suite-Laufzeit 30+ min → ~30 s). Dabei aufgedeckt:
  die „grünen" DoD-Vollläufe der Session waren durch `| tail`-Piping der
  Hintergrund-Läufe MASKIERT (Exit-Code von tail) — real waren 7 Fälle rot:
  6× Blockinvarianz bei block-intern interpolierenden Originalen (Tests per
  dokumentierter Konvention entfernt, Muster ConsoleLABuss) + Isolator3-
  Null-Test (resonante Biquad-Kaskade verstärkt Denormal-Guard-Rauschen
  ~60 dB → dokumentierte Toleranz 1e-4 statt 1e-6, kein Port-Bug). Danach
  Debug- UND ASan-DoD-Volllauf verifiziert grün (166 Fälle / 313815
  Assertions, Ausgabe gelesen statt Exit-Code vertraut).

**Fader↔Button-Modus pro dsp-Parameter (Dev-Modus) — FERTIG (03.07.2026):**

- **Konzept (User-Entscheidungen):** jede dsp-Parameter-Spalte des FxModulePanel
  kann auf benannte Wert-Buttons umgeschaltet werden (Dev-Zeile, dritter Toggle
  „btn"/„fdr"). Nicht-Dev: Buttons ERSETZEN den Fader (vertikale Stapel à 5,
  ab dem 6. ein zweiter Stapel daneben, Limit 10; Spalte verbreitert sich) —
  Klick ruft den Wert über den Fader-Pfad ab (paramValue ohne UndoManager,
  6.1). Dev: Fader UND Buttons gleichzeitig — Fader findet den Wert, Button-
  Klick SPEICHERT ihn (undo-fähig), +/−-Stepper bestimmt die Anzahl (nur hier),
  Doppelklick benennt um (Label-setEditable-Muster). Aktiver Button = LED-Stil
  (exactlyEqual über float). Motivation u.a. GlitchShifter/Tighten: ein
  Button-Sprung = EIN Geometrie-Wechsel statt Dutzender beim Fader-Sweep.
- **Datenmodell (Muster 4.6, wie userMin/userMax/curve):** per-Parameter-
  Patch-Properties `uiMode` (nur "buttons", fehlend = Fader) + `uiButtons`
  (EIN JSON-String-Property `[{"n":"Dry","v":0.25},…]` via juce::JSON —
  atomar undo-fähig, robustes Namens-Escaping, reist als XML-Attribut durch
  ModuleUiDefaults; var-Arrays überleben XML nicht, deshalb String). uiButtons
  bleibt beim Zurückschalten auf Fader geparkt (verlustfrei). Keine Migration
  nötig, OSC/CV/Control-Links unberührt.
- **APIs:** `ChassisSchema::parseButtons/buttonsToString/isButtonMode` (+
  Limits maxUiButtons=10, maxUiButtonsPerStack=5, Name ≤ 16 Zeichen);
  GraphManager `setParameterUiMode` / `setParameterButtonCount` (wachsen mit
  aktuellem Wert als „P{n}", schrumpfen von hinten, EIN Undo = ganze Liste) /
  `storeParameterButtonValue` (clamped auf Hard-Range) /
  `renameParameterButton`. ModuleUiDefaults nimmt beide Properties mit
  (applyTo validiert uiButtons defensiv via parseButtons).
- **UI:** `FxModulePanel::ValueButton` (Label-basiert wegen Doppelklick-
  Rename; onClick nur bei Einzelklick — der zweite Klick gehört dem Editor);
  variable Spaltenbreiten über `columnWidthFor`/`getPreferredWidth`
  (degeneriert ohne Button-Spalten exakt zu widthForColumns — bestehende
  Tests unverändert grün), NodeComponent-Sizing folgt getPreferredWidth.
  Friedhof-Mechanismus (retiredColumns) um valueButtons/modeButton/Stepper
  erweitert — Rebuild aus dem eigenen onClick bleibt crashfrei. Button-Höhe
  dynamisch (≥53px bei ≤3 Buttons, gekappt 34px — dokumentierte Ausnahme
  von der 44px-Regel analog 16px-Dev-Zeile).
- **Verifikation:** ConduitTests 298 Fälle / 11998 Assertions grün, Debug UND
  ASan (Ausgabe gelesen); ConduitAirwindowsTests 166 / 313815 grün. Neue
  Tests: parseButtons-Roundtrip/Limits/Robustheit, alle 4 GraphManager-APIs
  mit Undo, Defaults-Roundtrip, 5 UI-Fälle (Ersetzen/Stapel-Layout/
  Dev-Speichern/Stepper/Aktiv-Markierung).

**UI-Skalierung + App-weiter Dev Mode + Dev-Panel — FERTIG (03.07.2026):**

- **UiSettings** (Source/Core, Muster MeterSettings: ChangeBroadcaster +
  PropertiesFile `Conduit/Ui.settings`): uiScale 0.5–2.0, fontScale 0.8–1.4,
  devModeEnabled — die Klasse SPEICHERT nur; die Anwendung machen Main.cpp
  (Start, vor der Fenster-Erzeugung) und der EngineEditor (live als
  ChangeListener). Kein Test setzt je globalen Desktop-Zustand.
- **Globale UI-Skalierung wie Ableton:** `Desktop::setGlobalScaleFactor`
  (skaliert ALLE Fenster inkl. Dialoge; multipliziert sich aufs OS-DPI).
  Settings-Tab „Oberfläche" (UiSettingsComponent): Slider 50–200 % in
  10er-Rastern, Commit am Drag-Ende (das Fenster skaliert unter dem Slider
  weg — kontinuierlich wäre eine Feedback-Schleife) bzw. bei Bahn-Klick/
  Pfeilen/TextBox sofort.
- **Separater Schriftgrößen-Faktor** (80–140 %, 5er-Raster): zentraler
  Helper `push::scaledFont(height, medium)` + `get/setFontScale`
  (PushLookAndFeel) — alle direkten paint()-Textausgaben umgestellt.
  Kern-Trick: `getLabelFont`/`getTextButtonFont`/`getComboBoxFont`/
  `getPopupMenuFont`-Overrides skalieren beim ZEICHNEN — Labels behalten
  ihre unskalierte Basisgröße (setFont-Stellen blieben unangetastet, keine
  Doppel-Skalierung, kein applyFonts-Boilerplate). Live-Refresh: EngineEditor
  feuert `sendLookAndFeelChange()` über alle Desktop-Fenster (nur bei echtem
  Font-Delta — Full-Repaint).
- **Dev Mode als Einstellung:** Toggle im „Oberfläche"-Tab. NodeComponent/
  NodeCanvas bekamen einen 8. ctor-Parameter `UiSettings*` (Default nullptr —
  Alt-Tests unverändert; nullptr → DEV-Button sichtbar wie bisher). DEV-
  Toggle im Modul-Header ist nur noch im Dev Mode sichtbar; Deaktivieren
  setzt aktive Kachel-Dev-Modi zurück (setDevMode(false) + Farb-Reset),
  resized() reserviert den Header-Platz nur bei sichtbarem Button.
- **Schwebendes Dev-Panel** (Source/UI/DevPanel): DocumentWindow always-on-
  top, Inhalt = dieselbe UiSettingsComponent wie der Settings-Tab (derselbe
  Broadcaster → automatisch synchron). Zugang über das neue Dev-Tile der
  TransportBar (nur im Dev Mode sichtbar, LED = Panel offen, Muster „Status
  kommt vom Editor"); Close async via SafePointer, Dev Mode aus schließt
  das Panel automatisch.
- **Verifikation:** ConduitTests 305 Fälle / 12026 Assertions grün (Debug;
  ASan-Lauf siehe Commit), neue Tests: UiSettings (Defaults/Clamps/
  Roundtrip/Broadcast/defekte Datei), fontScale-Skalierung (scaledFont/
  getJost/LnF-Fonts, RAII-Reset), Dev-Mode-Gating (Sichtbarkeit, Reset,
  ctor-Erben, nullptr-Fallback).

**FX-Chassis-Standard für alle Audio-FX-Module (Plan: 7 Meilensteine M1–M7) — M1–M6 abgeschlossen:**

Ziel des Gesamtvorhabens (User-Plan 03.07.): jedes FX-Modul bekommt einheitlich
Ableton-artige I/O-Gain-Fader mit Meter, einen Link-Audio-Send-Button am Output,
alle DSP-Parameter als vertikale Fader-Reihe mit CV-Input + Attenuverter pro
Parameter (Mutable-Stil) sowie einen Dev-Modus (Range-Edit, uiHidden,
Bezier-Fader-Kurven, Modul-Typ-Defaults). Wird als CLAUDE.md 4.6 verbindlich.

- **M1 — Chassis-DSP + Schema + Migration (fertig):**
  - `ProcessorModule` ist vom Einzeiler zum FX-Chassis ausgebaut: Subklassen implementieren nur noch `prepareCore()`/`processCore()` (reine Stereo-Audio-Sicht) und liefern DSP-Parameter als `ChassisParamDesc`-Liste an den Konstruktor; `prepareToPlay`/`processBlock`/`appendParametersTo`/`getParameterTarget` sind final
  - Signal-Reihenfolge: noteBlockBegin → CV-Blockmittel → In-Gain (−60..+6 dB, 5-ms-SmoothedValue, −60 = exakt 0) → In-Meter → processCore → Out-Gain → Out-Meter → Link-Tap-commit; komplett lock-/alloc-frei (RT-Audit-Test)
  - **CV→Parameter-Modulation (neu im Projekt):** Kanal-Layout FEST Audio 0..1, CV 2..N (CV-Kanal von Parameter i = 2+i, eigener Discrete-Bus); `effective = clamp(base + cv·cv_amt·(hardMax−hardMin), hardMin, hardMax)`, Attenuverter `{param}_cv_amt` bipolar −1..+1; unverbundene CV-Kanäle sind vom Graph genullt → neutral
  - Parameter-Property `role` (`dsp`/`chassis`/`cvAmount`) fürs spätere UI-Layout; OSC-Adressen bleiben kanonisch, Auto-Registration greift ohne Zusatzcode; neue Schema-Ids `userMin`/`userMax`/`uiHidden`/`curve`/`linkSendEnabled` (M5/M6) definiert
  - Eigene 2×2-`LevelMeter`-Instanzen pro Modul (in/out); Link-Send-Tap-Grundgerüst (`LinkSendTaps`, `setSendEnabled`, atomarer rtTap, Phase-1-Retire via `releaseSessionResources`) — GraphManager-Weiterleitung + UI-Button folgen in M4
  - `ChassisSchema` (pure, testbar): Rollen-Konstanten, `computeEffective()`, idempotente Migration v1→v2 in `GraphManager::normalizeNode` für alle Processor-Nodes (Gains/Attenuverter/role ergänzen, `numInputChannels = 2 + numDsp`, Kanäle 0/1 stabil — Kabel und User-Werte überleben)
  - `AirwindowsProcessorModule` auf die zwei Core-Hooks geschrumpft (targets-Array/Schema/Bus entfallen), `stateVersion` → 2
  - **Verifikation:** 250 Testfälle / 10985 Assertions grün (Debug + ASan lokal). Neu: `ProcessorChassisTests` (13 Fälle — Schema/Rollen, Unity/Stille, klickfreie Rampe, bipolare CV-Modulation + Hard-Clamp + Blockmittel, Allocation-Audit, Meter post-Gain, Link-Send offline-safe, Migration idempotent + identisch zu createState)
  - Übergangszustand: das alte ParameterPanel zeigt die neuen Chassis-Zeilen (input_gain/output_gain/*_cv_amt) als normale Fader, CV-Ports erscheinen als zusätzliche Input-Ports — hübsch wird es in M2 (FxModulePanel)
- **M2 — Vertikale Fader-UI (fertig):**
  - `GainFaderMeter` (neu): Ableton-Kanalzug — vertikaler dB-Fader (Doppelklick = 0 dB), dB-Skala, integriertes Stereo-Meter (RMS/Peak/Peak-Hold/Clip-Feld mit Klick-Reset); Meter-Auflösung pro 30-fps-Tick transient über `GraphManager::getModuleFor` (Zombie-UI-Regel, Muster ScopeDisplay)
  - `FxModulePanel` (neu): Pflicht-Oberfläche aller Processor-Nodes — links In-Zug, Mitte pro dsp-Parameter eine vertikale Fader-Spalte (Titel + langer Fader), rechts Out-Zug; layoutet nach `role`, Gains/cv_amt erscheinen nicht als Spalten; zentrale Breitenformel `widthForColumns`
  - `PushLookAndFeel::drawLinearSlider`-Override: Push-/Ableton-Optik (dunkler Track, Füllung, rechteckiger Griffstein) für vertikale UND bestehende horizontale Slider app-weit
  - `NodeComponent`: Processor-Nodes (über `type == "Processor"`, nicht factoryKey) bekommen das FxModulePanel; Kachelgröße folgt der Spaltenzahl; Teardown-Phase-1 verdrahtet
  - Verifikation: 257 Testfälle / 11011 Assertions grün (Debug + ASan). Neu: `FxModulePanelTests` (Spalten nur für role=dsp, Fader↔Tree beidseitig, stopUpdates, Zombie-sicherer Meter-Paint ohne materialisiertes Modul, NodeComponent-Integration, Breitenformel)
- **M3 — CV-Inputs + Attenuverter in der UI (fertig):**
  - Pro Fader-Spalte: Attenuverter-Knob (Rotary, bipolar −1..+1, Doppelklick = 0, bindet `{param}_cv_amt`) + CV-Port (PortComponent, Kanal = 2+Spaltenindex) unter dem Fader
  - `NodeComponent::getPortCentre`/`findPortNear` delegieren CV-Kanäle ≥ 2 an `FxModulePanel::cvPortCentre` — Kabel-Zeichnung und Drop-Toleranz des NodeCanvas funktionieren unverändert; linke Kachelkante trägt nur noch die Audio-Eingänge (Kanäle 0/1)
  - `PushLookAndFeel::drawRotarySlider`: MI-Stil (Körper, Zeiger, Wert-Bogen ab Mittelstellung bei bipolaren Ranges)
  - Verifikation: 261 Testfälle / 11070 Assertions grün (Debug + ASan). Neu: CV-Knob-Bindung beidseitig, Port-Kanal-Layout, Anker-Delegation + findPortNear, End-to-End durch den ECHTEN Graph (EngineProcessor: In-ch1 als CV-Quelle auf Density-CV-Kanal 2 → Ausgang ändert sich messbar; cv_amt 0 = wirkungslos)
- **M4 — Link-Send-Button am Output (fertig):**
  - `FxModulePanel`: LINK-Button + Status-LED (offline grau / announced gelb / streaming grün, Farben wie StatusBadge) unter dem Output-Zug; Klick togglet den Send des Post-Output-Gain-Signals, Kanal-Name = moduleId
  - `GraphManager::setLinkSendEnabled` (undo-fähige Patch-Aktion) + Property-Listener-Zweig → `ProcessorModule::setSendEnabled` LIVE (Tap create/retire ohne Rebuild); `materializeModule` setzt den persistierten Send-Zustand VOR prepareForGraph (Preset-Load-Pfad)
  - Verifikation: 265 Testfälle / 11098 Assertions grün (Debug + ASan; TSan via CI). Neu: Toggle an/aus/Undo mit echtem LinkClock-Rig, Rename propagiert Sink-Name live, persistierter Send entsteht bei Materialisierung, Delete Phase 1 zieht Tap sofort zurück, Epoch-Retire-Handshake (Audio-Block-Surrogat), UI-Button undo-fähig + LED offline-safe
- **M5 — Dev-Modus + CV-Richtungs-Modell (fertig):**
  - DEV-Toggle im Node-Header (transient pro Kachel, orange aktiv); im Dev-Modus pro Spalte Min/Max-Editierfelder + Ausblenden-Toggle, ausgeblendete Spalten gedimmt ohne Port
  - `GraphManager::setParameterUserRange` (validiert gegen Hard-Range, clamped den Wert in DERSELBEN Undo-Transaktion) und `setParameterHidden` (trennt CV-Kabel des Parameters in derselben Transaktion — keine Phantom-Modulation; Bus-Layout bleibt IMMER unverändert). Nur role=dsp ist ausblendbar
  - **CV-Richtungs-Modell (User-Feedback aus dem Live-Test):** `effective = clamp(base + |cv|·amt·(userMax−userMin), userMin, userMax)` — Gleichrichtung VOR der Block-Mittelung (bipolare Quellen werden zur Modulations-Hüllkurve), die Richtung bestimmt allein der Attenuverter (rechts = vom Fader nach oben, links = nach unten; vorher war negativ bei Sinus-LFOs unhörbare Phaseninversion). Modulation strikt im Dev-Modus-Bereich; User-Range erreicht das Modul live (Property-Listener) und bei der Materialisierung — kein Rebuild
  - Friedhof-Muster im FxModulePanel: Spalten-Rebuild aus dem eigenen hideButton-Callback zerstört deferred (kein Use-after-free, Muster TransportBar)
  - `ChassisSchema::cvChannelForParam`: feste CV-Kanal-Zuordnung, uiHidden verschiebt nie Kanäle
  - Verifikation: 272 Testfälle / 11170 Assertions grün (Debug + ASan). Neu: Richtungs-Modell-Sektionen (Betrag, Richtung, User-Range-Skalierung/-Clamp, Rechteck-Gleichrichtung), setParameterUserRange/-Hidden inkl. Ein-Undo-Semantik, Live+Materialisierungs-Sync der Range, Panel-Dev-Modus (uiHidden nur im Normalmodus weg, Editierfelder committen, ungültige Eingaben restauriert), NodeComponent-DEV-Toggle mit Breiten-Nachzug
- **M6 — Bezier-Fader-Kurven + Modul-Typ-Defaults (fertig):**
  - **Fader-Kurven:** Parameter-Property `curve` ("x1 y1 x2 y2", kubische Bezier (0,0)→(1,1)); Kontrollpunkte via `parseCurve` auf [0,1] geclamped → x(t) UND y(t) monoton (CSS-Easing-Eigenschaft), Mapping eindeutig invertierbar. `CurvedSlider` (überschreibt `proportionOfLengthToValue`/`valueToProportionOfLength`) — REINES UI-Mapping, im Tree/OSC/CV/Preset steht immer der echte Wert. Bisektions-Löser in `ChassisSchema` (pure, testbar)
  - **CurveEditor** (CallOutBox am ~-Button jeder Spalte, Dev-Modus): zwei draggbare Kontrollpunkte, „linear"-Reset, UND die Min/Max-Felder des User-Regelbereichs integriert (User-Wunsch 03.07. — die kleinen Spalten-Editierfelder entfielen dafür); abgelehnte Range-Commits restauriert der Editor. Commits laufen undo-fähig über `GraphManager::setParameterCurve`/`setParameterUserRange`
  - **Modul-Typ-Defaults:** `ModuleUiDefaults` (App-Zustand, `Conduit/ModuleUiDefaults.settings`, Muster MeterSettings) — „als Standard"-Button im Dev-Modus sichert die dsp-Overrides (userMin/userMax/uiHidden/curve) pro factoryId; `GraphManager::addModuleNode` wendet sie bei NEU-Anlagen als Overlay an (Presets/Patches gewinnen immer); Capture ohne Overrides = Reset des Eintrags. EngineProcessor besitzt die Instanz
  - Verifikation: 279 Testfälle / 11261 Assertions grün (Debug + ASan). Neu: Bezier parse/eval/Invertierbarkeit/Monotonie, setParameterCurve undo-fähig + Validierung, CurvedSlider-Roundtrip, Panel-Kurve live, ModuleUiDefaults Capture→Overlay→Reset, addModuleNode-Overlay, CurveEditor-Range-Commit/Restaurierung
- **M6b — Control-Linking + Kurven-Editor-Ausbau (fertig, lokal committet):**
  - **Control-Linking (User-Entscheidungen: wie interne Modulation, modulintern):** Properties `linkSource`/`linkAmount` (−1..+1) pro dsp-Parameter; DSP zweistufig und ZYKLENSICHER — Stufe 1 = base+CV, Stufe 2 = `clamp(stufe1 + normQuelle·amount·userRange)`, beide Stufen lesen Stufe-1-Werte (A↔B harmlos, getestet); Link folgt auch OSC-/CV-Änderungen der Quelle, der Ziel-Fader bleibt stehen. `GraphManager::setParameterLink` (validiert dsp/≠Ziel, undo-fähig, Live-Sync + Materialisierung)
  - **Link-Response-Kurve** (`linkCurve`, z.B. Gain-Matching): formt die normalisierte Quelle vor der Modulation; alloc-frei im Audio-Thread (Bisektions-Bezier), `setParameterLinkCurve` undo-fähig, in ModuleUiDefaults enthalten
  - **CurveEditor-Ausbau (Screenshot-Feedback):** Tabs Fader/Link (Link nur mit Quelle wählbar); Fader-Plot zeigt das HARD-Range-Fenster, die beiden Range-ENDPUNKTE sind vertikal draggbar und setzen userMin/userMax direkt (Mindestabstand, Textfelder bleiben); Link-Zeile = Quellen-ComboBox + bipolarer Amount-Slider
  - **Fallende Link-Responses (User-Nachtrag):** `LinkResponse` = Bezier-Form + draggbare Start-/End-Endpunkte im Link-Tab (Format "x1 y1 x2 y2 startY endY", 4-Token-Altbestand kompatibel) — Ende < Start dreht die Richtung direkt in der Kurve (Auto-Gain: density hoch → out_level runter), zusätzlich zum Amount-Vorzeichen
  - Verifikation: 285 Testfälle / 11342 Assertions grün (Debug + ASan). Neu: Link-DSP (Richtung, CV-Follow, Zyklus, User-Range-Skalierung, Response steigend/fallend an/aus), parseLinkResponse-Formate, setParameterLink/-LinkCurve Validierung+Undo+Materialisierung, Endpunkt-Drag (Fader-Range + Link-Response), Tab-Verhalten, UiDefaults-Roundtrip inkl. Link
- **M7 — CLAUDE.md 4.6 (fertig):** verbindlicher FX-Chassis-Standard als neuer Abschnitt 4.6 (Core-Hooks, Signal-Reihenfolge, CV-Richtungs-Modell, Control-Linking, Schema-Regeln, Dev-Modus/UI-Kontrakt), Schema 6.2 um die Chassis-Properties ergänzt, Roadmap-Zeile eingetragen — **der FX-Chassis-Plan (M1–M7) ist damit komplett; jedes künftige FX-Modul erbt den Standard automatisch (nur prepareCore/processCore implementieren)**

**Davor: Tap-Tempo-Umbau: Monitor + Set-Commit (inspiriert vom M4L-Device „TAP and CHANGE Tempo BPM"):**

- **Modell-Wechsel:** Tappen misst das Tempo NUR (Session bleibt unberührt) — die neue **Set-Kachel** neben Tap zeigt das getappte Tempo als Monitor (cyan) und committet beim Klick zur Link-Session. Ersetzt das alte Auto-Commit beim (n+1)-ten Tap.
- **Endloses Tappen:** kein Timeout-Reset mehr — Pausen verwerfen nur das unplausible Riesen-Intervall (> 3 s), die Messung läuft weiter (Median über rollierendes 8er-Fenster, folgt Tempowechseln). Reset NUR durch **Gedrückthalten** der Tap-Kachel (Dauer einstellbar 0.3–3 s).
- **Tap ▾ (Chevron-Menü, `TapMenuPanel`):** optionaler **Auto-Commit ab Tap n** (2–8; fürs MIDI/OSC-Mapping des Tap-Buttons, wo kein Set-Klick möglich ist — ab Tap n committet jeder weitere Tap verfeinert weiter) + Reset-Haltedauer. Der Taps-Slider ist aus dem Link-Menü dorthin umgezogen.
- **TransportSettings:** neu `tapAutoCommit` (default aus) + `tapResetHold` (default 1.0 s); `tapCount` umgewidmet zur Auto-Commit-Tap-Anzahl.
- **Tap zählt beim DRÜCKEN** (`setTriggeredOnMouseDown`, Timing wie Hardware); Tempo-Kachel zeigt immer die Session (kein Preview-Kampf mehr).
- **Verifikation:** 230 Testfälle / 10851 Assertions grün (Debug + ASan lokal). Neue/umgebaute Tests: TapTempo (endlos ohne Commit, Pause-Toleranz, rollierendes Fenster folgt Tempowechsel, Auto-Commit ab Tap n, reset), TransportBar (Set-Kachel-Monitor + commitTapPreview, Auto-Commit-Pfad, resetTapMeasurement), TransportSettings-Roundtrip/Clamp der neuen Keys.

**Davor: Airwindows-Module im Graph nutzbar (Density/Slew/Spiral) — Meilenstein abgeschlossen:**

- **Ausgangslage:** die Airwindows-DSP-Portierung (Density/Slew/Spiral) war in einer parallelen Session in einem eigenen Git-Worktree (`feature/airwindows-prep`) entstanden, isoliert verifiziert und per PR #1 nach `master` gemergt — aber nur als eigenständige `ConduitAirwindows`-Library, nirgends im Root-Projekt eingehängt, kein Modul-Wrapper. Worktree wurde aufgelöst (User wollte zurück zu einem einzigen Checkout), lokaler `master` per `git pull --rebase` synchronisiert, `feature/airwindows-prep`-Branch aufgeräumt
- **`ProcessorModule.h`** (`Source/Modules/`): neue Kategorie-Basis nach dem Einzeiler-Muster von `UtilityModule`/`GeneratorModule` (`ModuleType::processor` — der Fall stand in `toString()` bereits bereit)
- **`AirwindowsProcessorModule`**: generischer Wrapper (kein Template — die DSP-Basis ist selbst polymorph) für beliebige `airwindows::AirwindowsPlugin`-Instanzen; iteriert generisch über `getNumParameters()`/`getParameterInfo()` für `appendParametersTo()`/`getParameterTarget()`. **Bewusst kein `SmoothedValue`**: `AirwindowsPlugin::process()` snapshottet Parameter bereits selbst blockkonstant (exakt wie beim VST-Original) — zusätzliches Sample-Ramping widerspräche dem dokumentierten, gegen die DoD-Tests verifizierten Originalverhalten. Fester Stereo-Bus (2 in/2 out)
- **Drei dünne konkrete Module** (`AirwindowsDensityModule`/`AirwindowsSlewModule`/`AirwindowsSpiralModule`, je ~10 Zeilen): reichen nur eine passende Plugin-Instanz + moduleId/Displayname an die Basis durch. `ModuleFactory`-Registrierung + drei neue Einträge im "+"-Browser (`EngineEditor::buildBrowserItems`)
- **Root-`CMakeLists.txt`**: `add_subdirectory(Source/DSP/Airwindows)` + `target_link_libraries` für `Conduit`/`ConduitTests` bewusst ganz ans Dateiende gesetzt (nach der Catch2-FetchContent) — sonst baut das konditionale `ConduitAirwindowsTests`-Target nicht mit (`if(TARGET Catch2::Catch2WithMain)` wäre beim früheren Einhängen noch false)
- **Verifikation:** `Tests/Core/AirwindowsModuleTests.cpp` (neu — Parameter-Roundtrip, `getParameterTarget`-Mapping, NaN/Inf-freier Parameter-Sweep pro Modul). `ConduitTests.exe` 10913 Assertionen/238 Testfälle grün (Debug + ASan), `ConduitAirwindowsTests.exe` weiterhin 2565/10 grün (Debug + ASan) — läuft jetzt automatisch im Hauptprojekt mit statt nur im isolierten Harness
- **App-Smoke (User-Screenshot bestätigt):** Density-Node über den Browser angelegt, mit `audio_in`/`audio_out` verkabelt — Pegel fließt sichtbar durch. Regler auf ~70 % gezogen: Ausgangspegel kippt hörbar von grün auf rot (echte Sättigung, kein Passthrough). Node gelöscht (Zwei-Phasen-Delete) — kein Absturz, App läuft weiter
- **Nächster Schritt (später):** weitere Airwindows-Ports, Fix des Stock-LFO-Bridge-Bugs (siehe unten)

**Davor: M4L-Stock-Device-Kopplung (Ableton Stock-LFO ↔ Conduit-LFO) — Exploration, Rate/Depth-Bridge pausiert:**

- **Live-12-Smoke des Announce-Protokolls bestätigt (7.4):** `Tools/Max/ConduitLFO/ConduitLFO.maxpat` geladen → Conduit legt automatisch die LFO-Kachel an (find-or-create über remoteId), Rate UND Depth in beide Richtungen live steuerbar — sauberer End-to-End-Beweis für Announce + Alias-Adressierung (`/conduit/remote/{remoteId}/...`) + Dual-State-Pfad (6.1). Kein Conduit-Code geändert, das Feature war bereits vollständig implementiert
- **Stock-Ableton-LFO-Rate ↔ Conduit-LFO-Rate direkt verkabelt:** generische OSC-Auto-Registration (7.1) greift ohne Zusatzcode — `LfoModule` exponiert `rate`/`depth` bereits über `appendParametersTo`/`getParameterTarget`. Einheiten-Mismatch identifiziert: Conduit rechnet in Zyklen/Beat (tempo-relativ, phasenstarr), Stock-LFO im Hz-Modus absolut — Umrechnung gehört bewusst ins Max-Patch, nicht ins DSP-Modul (`cyclesPerBeat = Hz × 60 / BPM` und umgekehrt)
- **Bug im User-Bridge-Patch gefunden, noch nicht behoben:** `expr`-Objekte in Max feuern bei JEDEM Inlet (nicht nur links) — der Tempo-Feed in den rechten Inlet löste vor Eintreffen eines echten Rate-Werts eine Berechnung mit `$f1=0` aus und nullte den Rate-Dial. Fix skizziert (Tempo über ein `f`-Objekt cold zwischenspeichern, `t f f` synchronisiert die Auslösung ausschließlich über den Rate-Wert) — **Umsetzung vom User auf später verschoben**
- **Nächster Schritt (später):** Fix im Bridge-Patch anwenden, danach eigentliche Conduit-M4L-Devices (analog `ConduitLFO`) für weitere Module bauen

**Davor: Push-3-Transport-Header (CLAUDE.md 10.0) — 6 Schritte, abgeschlossen:**

- **Schritt 1 — Design-Fundament:** Jost (Google Fonts, OFL) als BinaryData; `PushLookAndFeel` (Default-LnF der App: dunkle Kacheln, LED-Akzente, Jost app-weit); `PushIcons` — ALLE Symbole als `juce::Path` aus normiertem 0..1-Quadrat (vektorbasiert, DPI-unabhängig)
- **Schritt 2 — TransportBar + Browser:** ersetzt die Modul-Button-Toolbar komplett; „+" öffnet den ModuleBrowser (Module + Preset laden/speichern als CallOutBox); Undo-Kachel (Shift-Klick = Redo), Capture ⛶ (Klick = Export alle, Shift-Klick = Kanal-Panel), Skala-Combos umgezogen; Bausteine IconTile/TextTile/ValueTile (Drag + Inline-Edit, Editor-Destruktion deferred — kein Use-after-free im eigenen Callback)
- **Schritt 3 — Link-Transport:** Play ▷ = Link Start/Stop-Sync (LED folgt der Session, auch von Ableton aus); Link-▾-Menü (`LinkMenuPanel`): Sync-Toggle + **Clock-Offset ±100 ms** (Beat-Lese-Versatz in captureClockState, Muster 8.3); `TransportSettings` (App-Zustand, Muster MeterSettings) → EngineProcessor speist die LinkClock; Fixed Length/Automate als persistierte Looper-Toggles (Endless-Grundstein)
- **Schritt 4 — Tempo-Sektion:** Tempo-Kachel „120.00" (Vertikal-Drag + Doppelklick-Edit), **Tap-and-Commit** (n Taps erfassen mit cyan Preview, Tap n+1 committet; n im Link-Menü einstellbar, Median-robust, `TapTempo` mit injizierter Zeitbasis), **Nudge ±2 %** solange gehalten (DJ-Angleichen — Phasen-Versatz bleibt beim Loslassen), Positions-Anzeige „Takt. Beat. Sechzehntel" live (LinkClock::getBeatPosition inkl. Offset), **globaler Session-Swing** (Root-Property → ClockState; Sequencer mit lokalem Swing 0 folgen, lokal > 0 überschreibt — CLAUDE.md 4.5/6.2)
- **Schritt 5 — Metronom:** `Metronome` allocation-free NACH dem GraphFader (Capture-Tap bleibt sauber); sample-genaue Beat-Grenzen (floor-Überquerung), Downbeat oktavhöher (Cos-Burst, 20-ms-Decay), Ziel = wählbares Stereo-Paar mit echten ChannelNames-Labels im Link-Menü, Disable lässt den Tail ausklingen (kein Knacks); **akustischer Check durch den User steht noch aus**
- **Schritt 6 — Pages:** `PageHost` hinter den vier Push-Icons — Grid (Ω, AbletonOSC-Remote), Mixer (∥∥), Clip (▷▭, Fugue-Machine-Sequencer, CV+MIDI) als gestylte Platzhalter, Device (|||) = Patch-Canvas; CLAUDE.md 10.0 neu + Roadmap um Looper-/Mixer-/Grid-/Clip-Page und Capture-Netzwerk-Share ergänzt
- **Verifikation:** 225 Testfälle / 10785 Assertions grün (Debug + ASan lokal, TSan+ASan via CI). Neue Tests: PushIcons/PushLookAndFeel (Geometrie in Bounds, Jost lädt), TransportBar (Tempo-Commit **poll-basiert** — Link merged Commits asynchron, direkt aufeinanderfolgende setTempo können kurz vom Merge-Echo überdeckt werden, CI-Fund 02.07.; Pages-Radio, Capture-LED, Swing-Property, Tap-Commit), TapTempo (Median/Timeout/Commit-Zählung), TransportSettings-Roundtrip, LinkClock (Offset-Beat-Versatz, Start/Stop nur bei deaktiviertem Sync getestet — Tests starten nie fremde Sessions), Metronom (sample-genau über Blockgrenzen, Anker-Kanäle, Tail nach Disable, Anker-OOB, RT-Audit), PageHost-Umschaltung. Smoke: Header komplett in Jost, Browser legt Sequencer an, Link-Menü mit Offset/Taps/Metronom-Ziel, POS zählt live, Clip-Page-Platzhalter (transportbar_*.png, linkmenu_*.png, pages_smoke.png)

**Davor: OSC-Send + Max4Live-Announce-Protokoll (CLAUDE.md 7.3/7.4) — Schritte 1–5 implementiert (Ultraplan-Cloud-Session), gemerged; Live-Smoke ausstehend:**

*Schritt 1 — Send-Fundament:*
- **`OscAddress.h`**: gemeinsamer Adressbau für Receive-Registry und Send-Pfad (`parameterAddress`/`remoteAliasAddress` + `syncAddress`/`announceAddress`); `rebuildEndpoints()` nutzt ihn, Adress-Äquivalenz per Test gesichert
- **`OscSendSettings`** (Muster `MeterSettings`): Host/Port/Enabled in `Conduit/OscSend.settings`, Default 127.0.0.1:**9001** (Loopback-Schutz), Enabled default aus
- **`OscSendService`**: 30-Hz-Snapshot-Diff-Timer [Message Thread], `lastSent`-Cache mit Key `(nodeUuid, paramId)` (rename-sicher), ein `OSCBundle` pro Tick + Chunking >50, Cache-Pruning, Deleting-Nodes übersprungen, `IOscSink`-Seam für Tests; Aktivierung leert den Cache → impliziter Voll-Sync. Float-Diff beidseitig über `float` (`juce::exactlyEqual`) — `var` speichert double, sonst Dauersende-Schleife
- **Echo-Suppression**: `OscController::onRemoteValueApplied` (Callback-Seam statt Direktkopplung — Controller bleibt receive-only) → `noteRemoteValue()` impft den Cache VOR dem nächsten Tick; `EngineProcessor` verdrahtet, Service VOR Controller deklariert

*Schritt 2 — /conduit/sync + Settings-Tab „OSC":*
- **`OscController`**: sync-Erkennung VOR dem Endpoint-Lookup [Netzwerk-Thread], atomic Flag + AsyncUpdater → `onSyncRequested` [Message Thread, NACH `applyTreeUpdates`] → `sendFullDump()`
- **`OscSettingsComponent`** (Muster `CaptureSettingsComponent`): Empfangs-Status, Ziel-Host/-Port, Enable-Toggle; vierter Tab im `SettingsWindow`, Controls public für headless Tests

*Schritt 3 — Auto-Learn der Absender-IP:*
- **Learn-Probe** (`beginIpLearn`/`cancelIpLearn`): `juce::OSCReceiver` verwirft die Absender-IP → Receiver kurz trennen, eigener `DatagramSocket` bindet den Empfangsport (Bind-Retry gegen das Rebind-Fenster), `read()` liefert die IP des ersten Pakets; Ergebnis via Atomic + AsyncUpdater, Receiver wird bei Ergebnis/Timeout/Cancel/Destruktor restauriert. UI-Button mit SafePointer (Fenster darf während der Probe schließen)

*Schritt 4 — Announce + remoteId + Kachel-Tint:*
- **`/conduit/announce`** (`s:remoteId s:factoryKey s:trackName i:trackColour`, Float-Farbe toleriert): Netzwerk-Thread validiert + sammelt (eigener Lock), `onAnnounce` [Message Thread] → **`RemoteModuleBinder`** (find-or-create über remoteId: existiert → idempotent, nur Tint; neu → Whitelist + `addModuleNode` mit configure + `renameNode`, Kollision → Auto-Name)
- **Alias-Adressen** `/conduit/remote/{remoteId}/{paramId}` (receive-only, rename-fest) zusätzlich in der Registry; Send bleibt kanonisch. `id::remoteId`/`id::tintColour` im Schema 6.2 (dokumentierte Ausnahme zur Laufzeit-ID-Regel 6 — beidseitig persistent). `NodeComponent` zeigt den Tint als Streifen unter der Kopfzeile, folgt Re-Announces live

*Schritt 5 — Max-Testdevice + Doku:*
- **`Tools/Max/ConduitLFO/`**: `.maxpat` + `conduit_announce.js` + README — `live.thisdevice` (nicht loadbang) → Announce + 30-s-Heartbeat, persistente remoteId in hidden `live.numbox` („Stored Only"), Rate/Depth-Dials → Alias-Adressen, `udpsend` mit `host <ip>`-Umkonfiguration. **Kein Audio im Device** — der LFO läuft nativ in Conduit
- **CLAUDE.md**: neue Abschnitte 7.3/7.4, Schema-6.2-Erweiterung, Roadmap (OSC-Send/M4L-Announce/Max-Testdevice → v2.0)

- **Verifikation (Remote-Session, Build nur via CI — Egress-Policy blockt FetchContent lokal):** CI (Ubuntu, `tsan` + `asan-linux`, jetzt auch auf `claude/**`-Branches) grün pro Schritt — neue Suiten `OscSendServiceTests`, `OscSettingsComponentTests`, `RemoteModuleBinderTests` (inkl. `[announce][osc][threading]`-Dauerfeuer-Stresstest) plus IP-Learn-Tests (Loopback-Tests hidden `[osc][network][.]`, lokal via Tag). **Live-12-Smoke bestätigt (03.07.2026):** Max-Device (`ConduitLFO.maxpat`) → LFO-Kachel wird automatisch angelegt, Dial moduliert in beide Richtungen (Details siehe M4L-Stock-Device-Kopplung oben). **Weiterhin ausstehend (User, Windows):** Debug-Build + ConduitTests-Zahlen, App-Smoke (OSC-Tab, TouchOSC-Follow + /conduit/sync + IP-Learn), Re-Announce-Test nach Neustart

**Davor: OscController-Threading-Fix: audioQueue.push unter registryLock (Audit-Befund):**

- **Befund (Threading-Audit):** `oscMessageReceived` kopierte den Endpoint (inkl. `target`-Pointer auf das `std::atomic<float>` im Zielmodul) unter `registryLock`, gab den Lock frei und pushte ERST DANACH in die Audio-Queue — die Lebensdauer-Garantie im Header war ein Timing-Argument, kein Mechanismus. Wird der Netzwerk-Thread zwischen Registry-Read und Push lange genug präemptiert, landet ein stale target nach der Phase-1-Deregistrierung in der Queue → Use-after-free auf dem Audio Thread
- **Fix (`OscController`):** Endpoint-Lookup, Clamp und `audioQueue.push` im SELBEN `registryLock`-Scope (Push direkt aus dem Iterator, die Endpoint-Kopie entfällt). Da `rebuildEndpoints()` die Registry unter demselben Lock swappt, kann nach abgeschlossener Deregistrierung kein stale target mehr in die Queue gelangen — harte Invariante statt Timing. `push()` ist wait-free, der Audio Thread nimmt den Lock nie (3.1 gewahrt); der `treeUpdateLock`-Pfad (Pfad 2) blieb separat. Header-Doku auf die neue Invariante umgeschrieben
- **`EngineProcessor::releaseResources`-Guard:** neues `audioCallbackActive`-Atomic (Eintritt/Austritt in `processBlock`); vor dem Drain der `oscToAudioQueue` jetzt `JUCE_ASSERT_MESSAGE_THREAD` + `jassert (!audioCallbackActive)` — der SPSC-Consumer-Wechsel auf den Message Thread ist nur bei gestopptem Callback zulässig, die Annahme „Audio steht" ist damit explizit statt implizit
- **Verifikation:** 168 Testfälle / 10439 Assertions grün (Debug + ASan). Neuer Test: Registry-Rebuild mit entferntem Node → Message an die alte Adresse erzeugt keinen Queue-Push (`getNumReady() == 0`); der `[osc][threading]`-Stresstest baut die Registry jetzt nebenläufig zum Netzwerk-Dauerfeuer neu auf (deckt den neuen Lock-Scope, TSan-Ziel via CI). Smoke: App-Start, OSC-UDP-Paket live an :9000 (App stabil), sauberer Shutdown per WM_CLOSE mit verbundenem Link-Peer (osclock_smoke.png)

**Davor: Canvas-UX: Node-Drag-Fix + Kanten-Ausrichtung + Kopfzeilen-Griff (CLAUDE.md 10) — User-Feedback „Module verschieben ist merkwürdig":**

- **Echter Bug gefunden (erklärt das „sinnlose Raster"):** `mouseDrag` schrieb `positionX` in den Tree; der synchrone Listener (`applyTreePosition`) setzte die Component dabei aufs noch alte Tree-Y zurück, der folgende `positionY`-Write las genau dieses `getY()` — **vertikales Verschieben ging komplett verloren**. Da die Platzierungs-Kaskade Nodes auf 24px-Stufen anlegt, saßen alle Kacheln auf festen 24px-Zeilen. Fix: Zielposition einmal berechnen, beide Properties aus dem lokalen Wert schreiben
- **Kopfzeile ist jetzt Grifffläche:** das Titel-Label schluckte alle Drags (Header war komplett tot) — `titleLabel.addMouseListener` leitet an die Kachel weiter (`getEventRelativeTo` rechnet um), Doppelklick-Rename unverändert; gegriffene Kachel hebt sich per `toFront` über Nachbarn
- **Kanten-Ausrichtung statt Grid (User-Entscheidung, revidiert nach Touch-Test):** Snap-to-Grid stotterte auf Touch → verworfen, dekoratives 24px-Hintergrund-Gitter entfernt. Stattdessen `snapToSiblings`: Bewegung ist pixelgenau, nur innerhalb von 10px rasten Oberkanten (gleiche Höhe) und linke Kanten (bündig untereinander) an den Geschwister-Kacheln ein — X/Y unabhängig, Endpunkt-Kacheln zählen als Referenz. Preset-/Tree-Positionen laden weiterhin exakt (kein Snap in `applyTreePosition`)
- **Verifikation:** 167 Testfälle / 10434 Assertions grün (Debug + ASan). Neue Tests: Kachel-Drag end-to-end mit synthetischen MouseEvents (beide Achsen pixelgenau + Tree-Sync — deckte den Vertikal-Bug auf), Kopfzeilen-Drag mit Label-relativen Koordinaten, X/Y-unabhängiges Kanten-Einrasten. Smoke: zwei Attenuatoren per Doppelklick, einer am Kopfzeilen-Label gezogen — rastet 5px neben der Nachbar-Kante bündig ein, Canvas ohne Gitter (`dragsnap_smoke_before/after.png`)

**Davor: Eingebettete Link-Audio-Send-Taps + Stereo-Pairing am Audio-Eingang (CLAUDE.md 7.2) — Schritt 4 von 4:**

*Shutdown-Fix — abort() beim Beenden mit aktiver Link-Verbindung (User-Fund im Live-Smoke):*
- **Symptom:** „abort() has been called" beim Schließen von Conduit, NUR wenn ein Link-Peer verbunden war. **Diagnose per SIGABRT-Stacktrace** (temporäre Instrumentierung): `std::bad_function_call` → `terminate` auf dem Link-IO-Thread, im UdpMessenger-Bye-Pfad, ausgelöst durch die Konstruktor-Lambda von `BasicLinkAudio`
- **Ursache (SDK-Teardown-Race):** `enableLinkAudio(false)` (aus dem `InputLinkSend`-Destruktor beim Shutdown) postet Bye-Arbeit auf den Link-IO-Thread; `Controller::setChannelsChangedCallback` postet den Callback-Reset ebenfalls nur async (FIFO). Der Message Thread zerstört derweil die `LinkAudio`-Instanz samt Callback-Membern — die zuerst gequeute Bye-Arbeit feuert den ChannelsChanged-Pfad gegen die zerstörten Member. Mit Peer verbunden gibt es Byes zu senden → nur dann reproduzierbar
- **Fix (`LinkClock`):** das FINALE `enableLinkAudio(false)` (Refcount → 0) läuft um einen Message-Loop-Hop verzögert (AsyncUpdater; schnelles Re-Enable cancelt). Laufender Betrieb: Disable einen Loop-Durchlauf später (Idle-Sinks gratis). Shutdown: Loop steht → kein Disable-Posting mehr → der `~LinkAudio`-Teardown deaktiviert selbst racefrei (sein Callback-Reset liegt FIFO-vor der Teardown-Arbeit). Test-Seam `flushPendingAudioState()`
- **Verifikation:** vor Fix 1/1 Abort-Repro (WM_CLOSE mit announced Send + Live-12-Peer, Port-20808 verifiziert gebunden), nach Fix **5/5 Zyklen sauber** unter identischen Bedingungen. 166 Testfälle / 10423 Assertions grün, neuer Test „finales enableAudio(false) ist deferred" (deferred/Flush/Re-Enable-Cancel/Dtor mit ausstehendem Off); bestehende Disable-Assertions flushen jetzt explizit

*Schritt 4 — Send-UI an den audio_in-Kanal-Zeilen:*
- **`InputSendButton`** (`Source/UI/`): S-Toggle pro Port-ZEILE (Paar = ein Send am Anker) — Klick schreibt NUR das ChannelNames-Flag; Engine (`rebuildInputSends`, diff-basiert) und Port-UI (`rebuildPorts` ersetzt auch die Buttons) folgen dem Broadcast. Status-LED-Farben wie LinkAudioSendPanel (grau offline / gelb announced / grün streaming), 10-Hz-Poll vom `InputLinkSend` (atomics, kein Processor-Pointer), `stopUpdates()` im Teardown (5.3). Hit 24px (Port-Ausnahme vom 44px-Ziel)
- **`NodeComponent`**: audio_in-Zeile jetzt [Label · Balken · **S** · ∥ · Port], eigene 24px-Send-Spalte, Kachel 344px; Provider `InputLinkSend*` von EngineProcessor → EngineEditor → NodeCanvas → NodeComponent durchgereicht (nullptr in Tests)
- **Verifikation:** 165 Testfälle / 10418 Assertions grün (Debug + ASan). UI-Tests erweitert: ein Send-Button pro Zeile (Paar → 3 Buttons bei 4 Kanälen), Breite 344, Flag-Toggle → Broadcast-Rebuild konsistent. Smoke: persistierter Send (Analog-In-Paar) resumt nach App-Neustart als gelbes S (announced — genau die Stream-Kontinuität über Neustarts), Klick auf ADAT In 1 aktiviert/deaktiviert live
- **Live-12-Hardware-Smoke (01./02.07.2026, User): „hat perfekt funktioniert"** — Sends streamen zu Live, Toggles/Pairing im Live-Betrieb geübt (Settings zeigen mehrere umgeschaltete Kanäle). Einziger Fund: abort() beim Beenden mit aktiver Link-Verbindung → als Shutdown-Fix diagnostiziert und behoben (siehe oben)

*Schritt 3 — Input-Link-Send-Backend (`InputLinkSend` im EngineProcessor):*
- **`ChannelNames`**: `linkSendEnabled` pro physischem Kanal (App-Zustand wie das Pairing — läge der Send im Patch, würde jeder Preset-Load den Ableton-Stream abreißen). Port-API `isPortLinkSendEnabled`/`setPortLinkSendEnabled`, XML-Attribut `linkSend`, Prune-Regel erweitert
- **`InputLinkSend`** (`Source/Core/`): pro Anker-Port ein `LinkSendTaps::Tap`. **`applySends` diff-basiert am lebenden Sink**: Namens-Delta → `setName` (live), Breiten-Delta (mono↔Paar am selben Anker) → `setWidth` — nie retire+create; nur verschwundene Anker retiren. Pure `buildSpecs(ChannelNames&, channels)` leitet die Specs aus Enable+Pairing+Labels ab (Paar = EIN Spec am Anker, Name `audio_in/{Anker-Label}`). RT-Pfad: `rtSlots[anchor]`-Atomics (Anker = Index, kein Torn Read); `processBlock` übergibt IMMER zwei gültige Kanal-Pointer (Partner defensiv gedoppelt) — ein Breiten-Wechsel zwischen Bounds-Check und Commit kann nie out-of-range lesen (ersetzt das geplante gepackte anchor+width-Atomic). Anker außerhalb der Kanalzahl → `noteIdle`
- **`EngineProcessor`**: Member nach `linkClock` (Sinks sterben vor der Clock); ChangeListener auf `channelNames` → `rebuildInputSends()` (deckt Enable/Pairing/Rename/Device-Wechsel — `setActiveDevice` broadcastet); zusätzlich aus `syncHardwareIOChannels` (Schrumpfen retired). Commit im `processBlock` **zwischen `captureClockState` und `graph.processBlock`** (SessionState-Stash vorhanden, Buffer trägt noch den rohen Input) in eigener `rt::ScopedRealtimeSection`. `prepareToPlay` → `inputLinkSend.prepare`
- **Verifikation:** 165 Testfälle / 10414 Assertions grün (Debug + ASan). Neue Tests (`InputLinkSendTests`): buildSpecs (Enable/Pairing/Schrumpfen/Paar ohne Partner), **Handle-Identität bei Rename und mono↔stereo** (kein Retire), Retire + Refcount bei Send-aus, Commit nach captureClockState (announced, nie rejected), Anker außerhalb der Kanalzahl (kein OOB, ASan-gewacht), echter-Thread-Retire (TSan-Ziel), ohne Clock kein Tap. Smoke: App mit geseedetem `linkSend`-Flag (gepaarter Anker Analog In L/R) läuft stabil, Backend announcet beim Start

*Schritt 2 — Stereo-Pairing: Modell + Port-UI + Doppel-Kabel:*
- **`ChannelNames`**: `Entry.pairedWithNext` (App-Zustand am **physischen** Geräte-Kanal, wie userLabel — kein Undo, überlebt Preset-Load, Device-Matching). Port-API `isPortPairStart`/`setPortPairedWithNext` (Masken-Mapping am Rand via `toDeviceChannel`; bei Kanal-Lücke durch Teil-Auswahl wird das Paar nicht angezeigt, bleibt aber gespeichert). Konfliktregel: ein Kanal in höchstens einem Paar (Setter löst Anker k−1/k+1). XML-Attribut `paired`, Prune behält Flag-only-Einträge
- **Port-UI** (`NodeComponent`): pure `buildPortRows` (Paare → span-2-Zeilen), Paar = EIN `PortComponent` mittig zwischen den Kanal-Zeilen (Doppelpunkt-Marker); **Meter und Labels bleiben eine Zeile pro Kanal** (`channelRowY` getrennt vom Kabel-Anker). Koppel-Toggles (∥) in eigener 20px-Spalte zwischen Meter und Port (audio_in-Kachel → 320px); ChannelNames-Broadcast baut Ports live um. `getPortCentre` liefert für Paar-Kanäle denselben Port ∓3px versetzt → **die Doppel-Linie fällt im unveränderten Kabel-Rendering gratis ab**
- **`GraphManager`**: `addConnectionPair`/`removeConnectionPair` — beide Kabel in EINER Undo-Transaktion (5.5); zweites Kabel nur wenn destChannel+1 existiert und frei ist (Mono-Fallback dokumentiert). `addConnection` in `canConnect`+`appendConnectionChild` refaktoriert
- **`NodeCanvas`**: Drag vom span-2-Port → `addConnectionPair`; Kabel-Klick erkennt Paar-Zugehörigkeit (`pairAnchorForPort`) und trennt beide Linien in einer Transaktion
- **Verifikation:** 160 Testfälle / 10370 Assertions grün (Debug + ASan). Neue Tests: ChannelNames-Pairing (Anker/Konflikt/Teil-Masken-Verankerung/Persistenz inkl. Flag-only-Roundtrip), `buildPortRows` (Paare, letzter Kanal ohne Partner), Komponenten-Test (3 Ports/4 Meter/320px, ∓3px-Anker, Entkoppeln), Canvas-Drag → 2 Connections + EIN Undo entfernt beide, Mono-Fallback, `removeConnectionPair`. Smoke: Toggle koppelt „Analog In L/R" zu einem Port, EIN Drag → Doppel-Linie auf Analog Out L/R, EIN Klick trennt beide (Screenshots pairing_smoke_*.png)

*Schritt 1 — `LinkSendTaps` extrahieren (verhaltensneutral):*
- **`LinkSendTaps`** (`Source/Core/`): wiederverwendbare Send-Mechanik aus dem `LinkAudioSendModule` extrahiert — pro Tap ein Link-Kanal (sink + rtSink-Atomic + Status + Dither-Seed + Interleave-Buffer), TPDF-Konvertierung (`convertToInt16Tpdf`, Modul behält delegierende static → Dither-Tests wörtlich grün), Epoch-Retire-Handshake (AsyncUpdater-Self-Re-Dispatch, 100-ms-Deadline), `enableAudio`-Refcount-Balance (erster aktiver Tap aktiviert, letzter deaktiviert, Destruktor balanciert ohne Phase 1)
- **Design fürs Kern-Feature:** Tap-Punkt ist Sache des Aufrufers (`commit()` wo gewünscht → pre/post ohne Sink-Wechsel); Sink-Kapazität immer `block × 2` SAMPLES → **`setWidth()` schaltet mono↔stereo am LEBENDEN Sink um** (kein Neuanlegen — der Ableton-Stream reißt nicht ab; `BufferHandle::commit` nimmt `numChannels` pro Commit). Tap-Objekte leben als Pool bis zur Destruktion (stabile Adressen, `retireTap` gibt nur den Sink in die Retire-Liste, Reuse beim Re-Enable)
- **`LinkAudioSendModule`** verschlankt: InputSlot hält `Tap*` statt sink/rtSink/dither/status; `processBlock` = `noteBlockBegin()` + Gain-Scratch + `tap->commit/noteIdle`; Phase 1 = `taps.retireAll()`; AsyncUpdater/Retire-Mechanik aus dem Modul entfernt. Scratch-Guard explizit (schützte vorher implizit über den Interleave-Buffer)
- **Verifikation:** 156 Testfälle / 10285 Assertions grün (Debug + ASan) — alle 153 bestehenden unverändert, 3 neue `LinkSendTapsTests` (Lifecycle/Refcount/Pool-Reuse, **Breiten-Umschaltung am lebenden Sink** inkl. Kapazität `block × 2`, prepare wächst-nur + ohne Clock kein Tap). Smoke: LinkSend-Node über Dialog angelegt — Zeile mit LED/S-Badge/Attenuator/Auto-Namen wie vor dem Umbau

**Davor: Ableton-Style Pegelanzeigen für audio_in/audio_out (CLAUDE.md 10) — Meilenstein abgeschlossen:**

*Schritt 3b — Capture-Einstellungen als eigener Tab:*
- **`CaptureSettingsComponent`** (`Source/UI/`): Formular mit Schwelle, Hold, Pre-Roll, Ring-Puffer, **RAM-Limit (neu)**, Bit-Tiefe, Auto-Schwelle, „nach Export freigeben", Export-Ordner. Ring/Pre-Roll folgen der Resize-Policy (async Bestätigung bei aktiver Aufnahme), RAM-Warnung über den Service-Broadcast. Aus dem `CapturePanel` herausgelöst
- **`CapturePanel`** verschlankt: nur noch die Kanal-Zeilen (Capture-**Aktionen**: LED/Pegel/Einzel-CAP), volle Panel-Breite; die Einstellungs-Controls sind entfernt. Ctor jetzt `(CaptureService&, ChannelNames&)`. Aktionen bleiben oben erreichbar (Toolbar: CAP-Toggle für Einzelspuren, „Capture" für alles)
- **`SettingsWindow`**: dritter Tab **„Capture"** zwischen Audio-Gerät und Metering. `EngineEditor` reicht `CaptureSettings`/`CaptureService` durch
- **Verifikation:** 153 Testfälle / 10237 Assertions grün (Debug + ASan; Capture-Logik unverändert, weiter über `CaptureSettingsTests` abgedeckt). Smoke: Capture-Tab mit allen Werten (RAM-Limit 3 GB, Ordner-Pfad), Aktionen weiter in der Toolbar

*Schritt 3a — Einstellungen-Menü + konfigurierbares Clip-Reset:*
- **`MeterSettings`** (`Source/Core/`): App-Zustand (eigene `Meter.settings`, überlebt Preset-Load, kein Undo) — Clip-Reset-Modus `manual`/`automatic`. `getClipHoldSeconds()` = 0 (manuell) bzw. `autoClearSeconds` (2,5 s). ChangeBroadcaster
- **`LevelMeter`**: `setClipHoldSeconds` + per-Kanal Auto-Clear im `process()` (Latch verlischt nach der Haltezeit; 0 = nur manuell). `EngineProcessor` besitzt `MeterSettings`, lauscht als ChangeListener und speist beide Meter (`applyMeterSettings`)
- **`SettingsWindow`** (`Source/UI/`): non-modales `DialogWindow` mit `TabbedComponent` — **Audio-Gerät** (bestehende `AudioSettingsComponent`, nur mit DeviceManager) + **Metering** (Clip-Reset-Auswahl, bindet `MeterSettings`). Dark-Look. Toolbar: „Audio"-Button → **„Einstellungen"**, öffnet das Fenster
- **Verifikation:** 153 Testfälle / 10237 Assertions grün (Debug + ASan). Neue Tests: `MeterSettings` (Default/Mapping/Roundtrip/ChangeBroadcast), `LevelMeter` Auto-Clear (Hold 0 = Latch bleibt, Hold 0,5 s verlischt, erneutes Clippen resettet den Timer). Smoke: „Einstellungen"-Button → Fenster mit beiden Tabs (Umlaute korrekt via `fromUTF8`)

*Schritt 2 — Meter-UI (horizontale Balken, verbreiterte I/O-Kacheln):*
- **`LevelMeterBar`** (`Source/UI/`, Muster `ScopeDisplay`): horizontaler Balken pro Kanal, 30-fps-Timer, liest Peak/Peak-Hold/RMS/Clip lock-free vom `LevelMeter`-Provider. Zeichnet RMS-Füllung (pegelabhängig grün/gelb/rot), Peak-Marker-Linie, Peak-Hold-Tick und Clip-Feld (rot, Latch). Nur das Clip-Feld ist klickbar (`resetClip`, Default in diesem Schritt) — sonst fällt der Klick an die Kachel durch (Node-Drag). `normFromLinear`: dBFS-Mapping −60…0 dB
- **`NodeComponent`**: baut für I/O-Endpunkte eine Bar pro Kanal (`rebuildMeters`), verbreiterte Kachel (300 px), Layout pro Reihe `audio_in` = [Label · Balken · ○Port], `audio_out` = [○Port · Balken · Label]. Meter folgen der Kanalzahl (Schritt-B-Kopplung), Teardown stoppt sie (5.3). Provider `const/non-const LevelMeter*` von `EngineProcessor` → `NodeCanvas` → `NodeComponent` durchgereicht
- **Verifikation:** 148 Testfälle / 10225 Assertions grün (Debug + ASan). Neue Tests: eine Bar pro Kanal + verbreiterte Kachel, Meter folgen der Kanalzahl, normale Module ohne Meter, `normFromLinear`-dB-Mapping (0/−6/−60 dB, Clip-Klemmung, Monotonie). Smoke: verbreiterte Kacheln mit Balken pro Kanal (Label · Balken · Port)

*Schritt 1 — Meter-DSP-Backend (verhaltensneutral):*
- **`LevelMeter`** (`Source/Core/Capture/`): lock-free Sicht-Metering pro Kanal (getrennt vom capture-`InputMeter`) — RMS (~150 ms One-Pole), Peak (sofortiger Attack, ~1,5 s Release), Peak-Hold (~1,5 s halten, dann Abfall), Clip-Latch bei ≥ 0 dBFS mit `resetClip`. Feste Arrays bis `MAX_CAPTURE_CHANNELS`, atomics, allocation-free. Muster: `InputMeter`
- **`EngineProcessor`**: zwei Instanzen `inputLevels`/`outputLevels`, `prepare()` in `prepareToPlay`; `processBlock` misst Input beim Tap (roher Hardware-Input) und Output nach `graphFader.process()` (beide im `rt::ScopedRealtimeSection`). Getter `getInputLevels()/getOutputLevels()`
- **Verifikation:** 145 Testfälle / 10207 Assertions grün (Debug + ASan). Neue Tests (`LevelMeterTests`): Peak-Attack/Release-Ballistik, RMS-Konvergenz (Warm-Start), Peak-Hold hält über den Momentan-Peak, Clip-Latch + kanalweiser Reset, Out-of-range-Nullwerte. Verhaltensneutral → noch keine UI-Änderung

**Davor: Echte Hardware-Kanalzahl für Audio-I/O (CLAUDE.md 9) — Meilenstein abgeschlossen:**

*Nachtrag — Aktive Kanal-Auswahl respektieren (Bugfix):*
- **Problem:** Bei Teil-Auswahl im Audio-Setup (z. B. erste Kanäle deaktiviert) komprimiert der `AudioProcessorPlayer` die aktiven Kanäle (Port i = i-ter *aktiver* Kanal), aber `ChannelNames::getLabel` las stur den Namen an voller-Liste-Index i → es sah aus, als fielen immer die *hinteren* Ports weg, egal welche Kanäle deaktiviert wurden
- **`ChannelNames`** kennt jetzt die Aktiv-Kanal-Masken und mappt Port-Index → echten Geräte-Kanal-Index (`toDeviceChannel`, i-tes gesetztes Bit). `getLabel`/`getUserLabel`/`setUserLabel`/`get/setImagePath` mappen am Rand; User-Labels sind am **physischen Kanal** verankert (stabil beim Ein-/Ausschalten früherer Kanäle). Leere Maske → identisch (rückwärtskompatibel). `AudioDeviceController` reicht `getActiveInputChannels()`/`getActiveOutputChannels()` durch
- **Verifikation:** 140 Testfälle / 10184 Assertions grün (Debug + ASan). Neue Tests: Teil-Auswahl (Kanäle 1,3 → Port 0/1 = B/D), User-Label folgt physischem Kanal, Default-Fallback nutzt echte Kanalnummer, leere Maske identisch. **Live vom User bestätigt**

*Schritt C — Connection-Pruning (Phantom-Connection-Schutz):*
- **`EngineProcessor::pruneEndpointConnections(nodeId, asSource, validChannels)`**: entfernt beim Schrumpfen der Kanalzahl (kleineres Interface / Ausstecken) genau die Kabel, die einen jetzt verschwundenen I/O-Kanal referenzieren (Kanal ≥ validChannels). `audio_in` als Quelle (`sourceChannel`), `audio_out` als Ziel (`destChannel`). Rückwärts-Iteration, geräte-getrieben → **nicht undo-fähig** (verhindert Phantom-Connections beim Preset-Save, v1-Lektion 6). `syncHardwareIOChannels` ruft es nach dem Kanalzahl-Update; die Tree-Entfernung zieht Graph-Connection (GraphManager-Swap) und Kabel-Repaint (Canvas) nach
- **Verifikation:** 139 Testfälle / 10175 Assertions grün (Debug + ASan). Neue Tests: Schrumpfen 8→2 kappt genau die out-of-range Kabel (gültige bleiben), gleiche Kanalzahl lässt alle stehen, Ausstecken (0/0) kappt alle I/O-Kabel, fremde Kabel (kein I/O-Endpunkt) unangetastet

*Schritt B — Tree-Kopplung (Port-UI folgt der Hardware):*
- **`EngineProcessor::syncHardwareIOChannels(ins, outs)`**: koppelt die reservierten I/O-Tree-Nodes an die echte Device-Kanalzahl — `audio_in` bekommt `ins` Ausgangs-Ports (`numOutputChannels`), `audio_out` `outs` Eingangs-Ports (`numInputChannels`). Idempotent (schreibt nur bei Abweichung), geräte-getrieben → **nicht undo-fähig** (Umgebungs-Zustand wie `ensureIONodeStates`), negative Werte auf 0 geklemmt
- **`AudioDeviceController::applyActiveDevice`** ruft sie bei Start + jedem Gerätewechsel mit den aktiven Kanälen (`getActiveInputChannels().countNumberOfSetBits()`) — dieselbe Basis wie `findMostSuitableLayout`, damit Port-UI und Graph exakt dieselbe Zahl tragen
- **`NodeComponent`**: reagiert auf `numInputChannels`/`numOutputChannels`-Änderungen — `rebuildPorts()` baut die Ports neu, I/O-Endpunkte wachsen in der Höhe mit der Portzahl (`updateEndpointSize`, `touchTarget + maxPorts·30`), Re-Layout + Canvas-Repaint (Kabel folgen). Port-Bau aus dem Konstruktor in `rebuildPorts()` extrahiert
- **Verifikation:** 138 Testfälle / 10163 Assertions grün (Debug + ASan). Neue Tests: `syncHardwareIOChannels` (8/6, 0/2, Schrumpfen, Klemmen, andere Bank unberührt); NodeComponent-UI (Ports + Kachelhöhe folgen der Kanalzahl, Schrumpfen stellt Größe wieder her). **Smoke: gespeichertes Multichannel-Interface → `audio_in`/`audio_out` zeigen live einen Port pro Kanal (Analog/Headphones/CV-Gate/ADAT) mit echten Namen, Kacheln mitgewachsen**

*Schritt A — Bus-Fundament (verhaltensneutral):*
- **`EngineProcessor::isBusesLayoutSupported`**: expliziter Override, der jede diskrete I/O-Kanalzahl akzeptiert (Ausgänge ≥ 1, Eingänge auch 0 → Ausgabe-only-Interface, 9.1). Damit probiert der `AudioProcessorPlayer` in `findMostSuitableLayout` die **echte Device-Kanalzahl zuerst** und reicht sie via `graph.setPlayConfigDetails()` bis in den Graph durch
- **Erkenntnis:** Der Graph adaptiert die Kanalzahl auf Audio-Ebene bereits automatisch; der eigentliche Bruch lag nur in der ValueTree-/UI-Ebene (→ Schritt B/C behoben)

**Davor: Audio-Settings-Fenster — Grundstein für ASIO/CoreAudio/Linux (CLAUDE.md 9 / 13.2):**
- **`AudioDeviceController`** (`Source/Core/`): App-Layer-Bündelung von `AudioDeviceManager` + `AudioProcessorPlayer`. Kapselt das bisher in `Main.cpp::initAudio()` inline liegende Geräte-Handling. Lauscht als `ChangeListener` und wendet bei JEDEM Gerätewechsel dieselbe Glue-Logik an: ChannelNames-Kontext setzen + `audioSetupWarning` setzen/löschen. Persistenz via eigener `PropertiesFile` (`Conduit/AudioDevice.settings`, App-Zustand wie ChannelNames — überlebt Preset-Load, kein Undo). Force auf 48k/32 nur beim Erststart ohne gespeicherten Zustand; bewusste Nutzerwahl bleibt erhalten. Reiner Helfer `computeWarning(rate, buffer)` unit-testbar
- **`AudioSettingsComponent`** (`Source/UI/`): Wrapper um die native `juce::AudioDeviceSelectorComponent` (Treiber-Typ, Device, Samplerate, Buffer, Kanalauswahl — automatisch systemabhängig, unter Windows WASAPI/ASIO je nach SDK). Dark-Look via `LookAndFeel_V4` (Midnight-Scheme). Backend/Frontend entkoppelt → spätere eigene Combos risikoarm. Non-modal im `DialogWindow` (`launchAsync`, 13.2)
- **EngineEditor:** neuer Toolbar-Button „Audio" (nur im Standalone-Pfad; `createEditor()` ohne DeviceManager blendet ihn aus). `audioSetupWarning` folgt jetzt live dem Controller (Timer, setzt/löscht); die Warnung wird rechts geankert und nur reserviert, wenn sie Text trägt → Normal-Layout unverändert
- **Verifikation:** 134 Testfälle / 10123 Assertions grün (Debug + ASan). Neue Tests: `computeWarning` (Zielwerte, abweichende Rate, Buffer-Grenze 64/65, Warntext-Inhalt). Smoke: App → „Audio"-Button öffnet Dialog (Treiber/Device/Kanäle/Rate/Buffer); auf VB-Audio-Testgerät (480 Samples) erscheint die Warnung „48000 Hz / 480 Samples (Ziel: 48000 Hz / 32)" live in der Toolbar
- **Offen (Schritt 2, eigener Meilenstein):** Audio-In/Out-Module mit echter Hardware-Kanalzahl (Multichannel-Bus im EngineProcessor, `audio_in/out`-Node-Kanäle an aktives Device koppeln, Connection-Pruning bei Gerätewechsel)

**Davor: Multi-Input Link Audio Send, Schritt C — UI-Panel + Anlege-Dialog (CLAUDE.md 7.2 / 10):**
- **`LinkAudioSendPanel`** (`Source/UI/`, Muster SequencerControlPanel): pro Eingang eine Zeile — Status-LED (offline/announced/streaming, per-Slot via `getSlotStatusForUi`, transiente Modul-Auflösung 10 Hz), Name-Editor (Doppelklick → `inputUserName`; leer = zurück zum Auto-Namen, dezenter dargestellt), Mono/Stereo-Badge (M/S), Attenuator-Slider (schreibt `in{n}_gain` in den Tree). Footer: **„Auto-Namen"**-Knopf → `refreshAutoNames`. Bindung nur an den Subtree (5.3), externe Änderungen (Snapshot/Undo/OSC) folgen über den ValueTree-Listener
- **`LinkSendCreateDialog`** (`Source/UI/`): kompakter Anlege-Dialog (Mono-/Stereo-Anzahl per Stepper, „Erstellen"), per CallOutBox angezeigt (kein Modal-Loop 13.2); `buildModes` = Monos dann Stereos, garantiert ≥ 1 Eingang. Der „+ LinkSend"-Toolbar-Button öffnet ihn und legt den Node via `addModuleNode`-Konfigurator mit `applyInputConfig` an
- **NodeComponent:** eigenes Panel statt generischem Slider für Send-Nodes; Kachelhöhe folgt der Eingangszahl; Teardown stoppt das Panel (5.3). Der alte `LinkAudioStatusBadge` wird für Send nicht mehr genutzt
- **Verifikation:** 130 Testfälle / 10109 Assertions grün (Debug + ASan). Neue UI-Tests: eine Zeile pro Eingang + Attenuator→paramValue, Name-Editor→userName (+ live Sink-Rename, leer=Auto), Refresh-Knopf zieht autoName + Label folgt, Dialog-`buildModes`. Smoke: Dialog (1 Mono + 1 Stereo) → Node mit 3 Ports, 2 Zeilen (M/S + Attenuator + LED), Auto-Namen-Knopf; „Link: 4 Peers" nach Firewall-Fix
- **Meilenstein Multi-Input Link Audio Send damit komplett** (Schritte A–C)

**Davor: Multi-Input Link Audio Send, Schritt B — Auto-Naming (CLAUDE.md 7.2 Schritt 3):**
- **Reiner Resolver `resolveSourceLabel`** (`Source/Core/SourceNameResolver`): rückwärts dest→source über `<Connections>`; Quelle audio_input → ChannelNames-Label (Fallback „In N"), sonst Quell-`moduleId` (+ Kanal-Suffix `:{n}` bei Multi-Output). Rein funktional, ohne Link/Audio/Device unit-testbar
- **Snapshot beim Verbinden** (`GraphManager::valueTreeChildAdded`): frisch gezogenes Kabel an einen Send-Eingang → Quell-Name EINMAL in `autoName` (nur wenn userName UND autoName leer; non-undoable, abgeleitet). Kein Live-Follow → Ableton-Routing bleibt stabil, wenn die Quelle umbenannt wird
- **Refresh** (`GraphManager::refreshAutoNames`): zieht `autoName` für alle Eingänge aus der aktuellen Quelle neu (eine Undo-Transaktion); `userName` bleibt und überschreibt weiter
- **Live-Sink-Rename:** `userName`/`autoName`-Änderung → `ISendConfigClient::inputNameChanged` → `sink.setName({moduleId}/{effektiverName})` ohne Rebuild; effektiver Name = userName ?: autoName ?: „input{n}"
- **Verdrahtung:** GraphManager bekommt `setChannelNames` (Owner EngineProcessor)
- **Verifikation:** 126 Testfälle / 10089 Assertions grün (Debug + ASan). Neue Tests: Resolver (keine Verbindung/audio_input/Modul+Suffix), Snapshot beim Verbinden, userName-Override, Snapshot bleibt bei Quell-Rename stabil, Refresh übernimmt neu
- **Offen:** Schritt C — UI-Panel (Attenuator/Name/Status pro Zeile, Refresh-Knopf) + Anlege-Dialog (Mono/Stereo-Anzahl)

**Davor: Multi-Input Link Audio Send, Schritt A — Modul-Kernumbau (CLAUDE.md 7.2):**
- **Reiner Sender statt Stereo-Pass-Through:** `LinkAudioSendModule` hat KEINEN Output-Bus mehr (`numOutputChannels=0`, Sink-Endpunkt wie audio_output). Der Signalfluss zum eigentlichen Ziel läuft per **Fan-out** am Ausgang der Quelle (GraphManager::addConnection erlaubt beliebige Fan-outs). Through-Modus/Output-Frage damit erledigt
- **Fixe, konfigurierbare Eingangszahl (kein dynamischer Bus):** pro Eingang mono/stereo, jeder ein eigener Link-Kanal + eigener Attenuator (Gain 0..1, SmoothedValue). Das Kanal-Layout wird via neuem Mixin `ISendConfigClient::applySendConfig` EINMAL vor prepareForGraph injiziert (`setChannelLayoutOfBus(discreteChannels(N))`, `isBusesLayoutSupported`); Eingangszahl beim Anlegen fix → keine Re-Materialisierung, kein Fade-Glitch, stabile Ableton-Kanäle. Mehr Kanäle = zweiter Send-Node
- **Neues Schema `<Inputs>`** (inputId stabil/serialisiert, mode, userName, autoName, gainParamId) + flache `in{n}_gain`-Parameter (GraphManager-Sync/OSC unverändert). **Kanal-Name = `{moduleId}/{effektiverName}`** (Node-Präfix → eindeutig über mehrere Send-Nodes); effektiver Name = userName ?: autoName ?: "input{n}"
- **Multi-Sink:** `std::vector<InputSlot>` (je Sink/rtSink-Atomic/gainTarget/SmoothedValue/Dither-Seed), in prepareToPlay einmalig gebaut (keine Audio-Callback-Reallokation); processBlock pro Slot Gain (ein Ramp/Frame) → TPDF-Dither → commit. Epoch-Retire über ALLE Sinks gebündelt (Phase 1 / Dtor). Refcount einmal pro Modul
- **Migration v1→v2** (`normalizeNode` ruft `LinkAudioSendModule::migrate`): alte feste Stereo-Sends → 1 Stereo-Eingang, `autoName` = alte moduleId (Namensstabilität), 0 Ausgänge; idempotent. Alte Output-Kabel werden still verworfen (Alpha-Caveat)
- **GraphManager:** `applySendConfig`-Injektion (aus `readInputConfig(tree)`); `addModuleNode` bekommt optionalen Konfigurator-Callback (für den Anlege-Dialog in Schritt C)
- **Verifikation:** 122 Testfälle / 10073 Assertions grün (Debug + ASan). Neue Tests: Migration (idempotent), Schema/Offsets, readInputConfig (userName>autoName>default), getParameterTarget-Mapping getrennter Slots, gemischte 2-mono+1-stereo-Materialisierung (4 Kanäle, 0 Out), Multi-Sink-Rename/Retire. Smoke: Send-Node rendert mit 2 Eingangs-Ports, 0 Ausgängen, Badge „announced"
- **Offen:** Schritt B — Auto-Naming (Snapshot beim Verbinden aus der Quelle + Refresh-Knopf); Schritt C — UI-Panel (Attenuator/Name/Status pro Zeile) + Anlege-Dialog (Mono/Stereo-Anzahl). Anmerkung: geplante Schritte A+B zusammengeführt (Schema/Bus + Multi-Sink lassen sich nicht ohne kaputten Zwischenzustand trennen)

**Davor: Link Audio Receive, Schritt 1 — LinkClock-Empfangsinfrastruktur (CLAUDE.md 7.2 Schritt 3, verhaltensneutral):**
- **Header-sichere Kanal-Identität `LinkClock::ChannelKey` (uint64):** die opake 8-Byte-Link-`NodeId` Big-Endian gepackt (Pack/Unpack in der .cpp), damit kein Link-/asio-Header in Projekt-Header leckt (IWYU-Falle 7.2). Bewusst NICHT serialisierbar — ChannelIds werden pro Session neu vergeben, Peer-Kanäle sind discoverbar, nie Teil des Patches (CLAUDE.md 6, v1-Phantom-Connection-Lektion)
- **Discovery-API `availableChannels()` [MT]:** wrappt `link.channels()` → `{ id, name, peerName }`; Änderungen melden sich über den bestehenden `ChangeBroadcaster` der LinkClock (ChannelsChanged, Link-Thread → `MessageManager::callAsync`)
- **`LinkClock::Source` (Pimpl-Wrapper analog `Sink`):** kapselt `ableton::LinkAudioSource`; der Empfangs-Callback (Link-Thread) rechnet DORT bereits das Beat-Alignment — `Info::beginBeats(sessionState, quantum)` gegen den frisch gecaptureten SessionState — und liefert dem Empfänger einen beat-gestempelten `ReceivedBuffer` (Samples nur während des Callbacks gültig, synchron herauskopieren). `nullopt` bei fremder Link-Session → Empfänger verwirft (nie naiv FIFO'en, v1-Drift-Lektion). Member-Reihenfolge so, dass `LinkAudioSource` zuerst destruiert (kein Link-Thread-Callback referenziert `this` nach der Freigabe); Teardown-Race gegen den Audio-Thread löst später das Modul über das zweiphasige Delete
- **Verhaltensneutralität belegt:** alle 119 Testfälle (10040 Assertions) grün — Debug UND ASan; neuer Test (`Tests/Core/LinkAudioSendTests.cpp`): ChannelKey-Round-Trip inkl. Grenzwerte (0, all-ones, 1) + Discovery-Struktur. App + Tests linken sauber; keine UI-Änderung
- **Offen (Schritt 2):** `LinkAudioReceiveModule` mit beat-aligned Jitter-Buffer (eigene header-only, ohne Link unit-testbar — hier landet die Alignment-Test-Suite), Int16→Float, zweiphasiges Delete der Source, Discovery-UI über den Broadcast, Monitoring-Latenz dokumentiert

**Davor: ChannelNames — benutzerdefinierte Namen für Hardware-Kanäle (`Source/Core/ChannelNames`):**
- **App-Zustand, KEIN Patch-Zustand** (gleiche Trennung wie CaptureSettings): Mapping (deviceKey, direction, channelIndex) → { userLabel, imagePath } — imagePath ist persistierter Platzhalter fürs spätere Kanal-Bild. Persistenz in EIGENER Datei `Conduit/ChannelNames.settings` (eine geteilte PropertiesFile mit den CaptureSettings würde sich beim Speichern gegenseitig mit veralteten Werten überschreiben — im Header begründet)
- **Device-Matching wie CalibrationProfile 8.1:** exakt → Prefix (Suffix " (N)" beidseitig ignoriert, `stripDeviceSuffix`) → kein Match; Schreiben bei Prefix-Match aktualisiert das bestehende Profil der Hardware-Familie. Default ohne Eintrag: vom Device gemeldeter Kanalname (`getInputChannelNames`, von Main.cpp nach initAudio als aktiver Kontext gesetzt), Fallback "In N"/"Out N". `ChangeBroadcaster` bei Änderungen; alle Methoden Message Thread
- **Eine Quelle, überall angewendet:** CapturePanel-Hardware-Zeilen zeigen das effektive Label, Doppelklick/Long-Press (500 ms, eigenes NameLabel) öffnet den Inline-TextEditor (kein Modal-Loop 13.2; leer = zurück zum Default; Tap-Zeilen nicht editierbar — Rename am Node-Titel); Export-Dateinamen nutzen das sanitierte Label (`sanitizeFileLabel`: verbotene Zeichen → `_`, Trim, 48 Zeichen; Provider-Hook `CaptureService::hardwareTrackName`, unverdrahtet → "inN" wie bisher); die I/O-Endpunkt-Nodes (audio_input/audio_output) malen die Labels neben ihre Ports (Touch hat keinen Hover) und setzen sie als Tooltip (PortComponent jetzt SettableTooltipClient, TooltipWindow im Editor). Richtungs-Mapping beachtet: audio_input trägt OUTPUT-Ports → Input-Labels
- **Tests (`Tests/Core/ChannelNamesTests.cpp`):** Matching exakt/Prefix (beide Richtungen)/kein Match, Default-Fallbacks (gemeldeter Name → "In N"), Trim/Längen-Limit, Richtungs-Trennung, Dateinamen-Sanitizing, Persistenz-Roundtrip inkl. Löschen
- Smoke verifiziert (ohne Input-Device auf der Dev-Maschine): geseedete `ChannelNames.settings` → audio_out-Ports zeigen die userLabels nach Neustart („Main L/R"); Default-Pfad zeigt gemeldete Device-Namen („Output channel 1/2") bzw. "In N"

**Davor: CaptureTapModule — Capture für effektierte Signale aus dem Graph (factoryId `capture_tap`, UtilityModule 4.1):**
- **Virtuelle Capture-Kanäle im CaptureService:** `registerVirtualChannel(name)` / `unregisterVirtualChannel(handle)` [MT] vergeben bis zu `MAX_VIRTUAL_CHANNELS = 8` Registry-Slots; im Puffersatz liegen die Einträge HINTER den Hardware-Kanälen (Index = numChannels + Slot) und nutzen exakt dieselben Pfade — PreRoll, Gate, Ring, BufferPool, Auto-Kalibrierung (`InputMeter::processChannel` misst Tap-Daten mit identischer Ballistik, Warm-Start jetzt pro Kanal statt global). Hardware + virtuell teilen `MAX_CAPTURE_CHANNELS`, das RAM-Budget der Ring-Dimensionierung und den Pool
- **Kein Materialverlust durch Taps (Design-Entscheidung):** Sätze werden nur für tatsächlich registrierte Slots dimensioniert — ohne Taps ist der Satz identisch zum reinen Hardware-Betrieb (bestehende RAM-/Resize-Tests unverändert). Braucht ein neuer Slot Puffer, wird bei inaktiven Kanälen still reallokiert (Handoff-Protokoll, verlustfrei); bei aktiven Kanälen wartet die Erweiterung auf den Guard-Tick (`needsVirtualExpansion`) — laufende Aufnahmen werden NIE für einen Tap verworfen, der Tap nimmt bis dahin nichts auf (UI-Zeile bleibt stumm)
- **Sample-Alignment:** `writeVirtualChannel()` [Audio Thread, aus Modul-processBlock] stempelt mit derselben SampleClock (blockStart = now − numSamples, die Clock tickt am Tap-Ende) — Capture All exportiert Hardware- und Tap-Spuren sample-aligned in einem Job; Export-Spurname = registrierter Kanal-Name (moduleId + `_l`/`_r`) statt `inN`
- **Modul + Lifecycle:** `CaptureTapModule` (2 In / 2 Out, Output reines Pass-Through — mitten in eine Kette patchbar) implementiert das neue Mixin `ICaptureTapClient` (Muster ILinkAudioClient): GraphManager injiziert Service + moduleId VOR `prepareForGraph` (Registrierung dort, idempotent; volle Registry → `Result::fail` → nodeError); Rename propagiert live via `setVirtualChannelName`; Delete Phase 1 (`releaseCaptureResources`, 5.3) trennt den Schreibpfad sofort atomar (rtService/rtSlots-Atomics, kein Epoch-Handshake nötig — der Service überlebt den Graph, `captureService` dafür im EngineProcessor VOR `graph` deklariert, gleiche Lektion wie linkClock), laufendes Material bleibt als **held** erhalten (Export/Reclaim wie Hardware, `CaptureGate::close()` neu); Slot wird erst nach Freigabe wiedervergeben
- **UI:** CapturePanel zeigt genutzte Taps als zusätzliche Zeilen (gleiche LED/Pegel/Floor-Marker/Einzel-Capture) im Abschnitt „Taps" unter den Hardware-Kanälen; Zeilenname = Spurname; Register/Unregister/Rename feuern ChangeBroadcasts des Service
- **Dokumentierte Grenzen (Modul-Doku):** Taps liegen IM Graph — Topologie-Swaps (5.2, ~5-ms-Fades) sind in Tap-Aufnahmen hörbar, in Hardware-Captures nicht; Plugin-/Modul-Latenzen im Signalweg werden nicht kompensiert (Folgethema)
- **Tests (`Tests/Core/CaptureTapTests.cpp`):** Registrierung/Grenzen/Slot-Reuse (inkl. geteilte 64er-Obergrenze), Schreibpfad + Deregistrierung → held ohne neue Daten + Slot-Sperre bis Freigabe, aufgeschobene Satz-Erweiterung bei laufender Aufnahme (Ring unverändert, Guard holt nach), Alignment-Beweis Hardware↔Tap (gleicher Impuls in beide Pfade im selben Callback → BWF-Export → identischer Sample-Index in beiden Dateien), gemischter BufferPool (geteiltes RAM-Budget, Aushungern + Recycling über Hardware- und Tap-Kanäle), Modul-Lifecycle end-to-end über den GraphManager (Pass-Through bitidentisch, Rename, Delete Phase 1/2, nodeError bei voller Registry, Destruktor ohne Phase 1)

**Davor: Link Audio, Schritt 2 — LinkAudioSendModule (CLAUDE.md 7.2, factoryId `link_audio_send`):**
- **Modul-Hierarchie 4.1 materialisiert:** `IOModule` + `NetworkIOModule` als Basisklassen; `LinkAudioSendModule` (2 In / 2 Out Stereo, Output = reines Pass-Through — mitten in eine Kette patchbar) implementiert `ILinkAudioClient` (neues Mixin-Interface 4.2) + `IClockSlave`
- **RT-Schreibpfad:** `LinkClock::Sink::commitFromClockState()` [Audio Thread, RT-safe] — `captureClockState()` stasht den SessionState des Blocks im Pimpl (Audio-Thread-only, kein Atomic), commit nutzt exakt die SessionState/Beat/Quantum-Basis des lokalen Renderings; **kein zweites captureAudioSessionState im Modul, der `ClockState` brauchte KEINE Erweiterung** (beatAtBlockStart + sampleRate standen schon drin). Float→Int16 mit TPDF-Dither (LCG-Differenz zweier Uniforms, ±1 LSB, deterministisch pro Seed) in vorallokierten Member-Buffer; Sink-Größe in SAMPLES (`samplesPerBlock × Kanäle`)
- **Sink-Lifecycle:** GraphManager injiziert Clock + moduleId via `setLinkClock()`/`ILinkAudioClient` VOR `prepareForGraph` (Sink entsteht in `prepareToPlay`, Kanal-Name == moduleId); `renameNode` (auch Undo) propagiert live via `sink.setName()`; Delete Phase 1 ruft `releaseSessionResources()` — Sink sofort weg (Pattern OscController), `enableAudio`-Refcount balanciert über Phase 1 UND Destruktor (Preset-Load/Shutdown ohne Phase 1)
- **Epoch-Handshake gegen das Teardown-Race:** Phase 1 trennt den Audio-Thread per `rtSink`-Atomic (seq_cst), die Sink-Destruktion wartet via AsyncUpdater-Self-Re-Dispatch (Muster 5.2 Schritt 3), bis nach dem Store ein neuer Block begonnen hat (`blocksProcessed`-Zähler); 100-ms-Deadline für gestopptes Audio. Begründung der seq_cst-Korrektheit in der Modul-Doku
- **Lebensdauer:** `linkClock` im EngineProcessor VOR `graph` deklariert (Module im Graph halten Sinks — Clock muss die Graph-Destruktion überleben), `WeakReference<LinkClock>` als Shutdown-Netz im Modul
- **UI:** `LinkAudioStatusBadge` am NodeComponent (LED + Text: offline/announced/streaming, 10 Hz, transiente Modul-Auflösung pro Tick — Muster ScopeDisplay, kein Processor-Pointer). Grenze dokumentiert: Erkennung über commit-Aktivität — Overrun ist von „kein Subscriber" über die Link-API nicht unterscheidbar (fällt auf announced zurück); ohne laufendes Audio ändert sich der Status nicht
- **Tests (`Tests/Core/LinkAudioSendTests.cpp`):** Dither-Statistik (Mittelwert ~0, Fehler ≤ 1.5 LSB, beide Nachbarstufen getroffen, Seed-deterministisch), Stereo-Interleaving mit Sentinel-Schutz (Frames/Samples-Grenzfall), Sink-Kapazität in SAMPLES + wächst-nur-Semantik, GraphManager-Lifecycle end-to-end (Materialisierung, Rename + Undo, Delete Phase 1/2, Refcount über zwei Module), Destruktor-Balance ohne Phase 1, Retire-Handshake unter echtem Audio-Thread (TSan-Ziel)

**Hardware-Smoke-Checkliste Link Audio Send gegen Live 12 Beta (12.06.2026, gleiche Maschine, 48 kHz / 480 Samples):**
- [x] Peer „Conduit" sichtbar, Kanal erscheint unter der moduleId (`link_audio_send_1`)
- [x] Live subscribt (Track-Input „Conduit") → Audio kommt an, Badge wechselt auf „streaming" (grün); Live-Preferences zeigen „Connected, 3.93 ms buffered"
- [x] Rename in Conduit (`drums`) → Kanal-Name in Live folgt live, Stream läuft ohne Unterbrechung weiter
- [x] Delete des Moduls → Kanal verschwindet aus der Session; Lives UI quittiert den Stream-Abriss erst nach ~5 s (Live-seitige Erkennungslatenz, Beta — Sink-seitig passiert der Reset sofort in Phase 1)
- [ ] 30 min Streaming bei 48 kHz / 32 Samples ohne xruns (Badge bleibt grün) — Langzeitlauf offen
- Stolperstein dokumentiert: „keine Peers" trotz aktivem Link-Schalter in Live → Lives Link-Engine hatte den UDP-Port 20808 nicht gebunden; kompletter Live-Neustart bindet neu (objektiv prüfbar via `Get-NetUDPEndpoint -LocalPort 20808` — beide Apps müssen gelistet sein)

**Davor: Link Audio, Schritt 1 — LinkClock auf ableton::LinkAudio (CLAUDE.md 7.2, verhaltensneutral):**
- `LinkClock`-Pimpl hält jetzt die einzige `ableton::LinkAudio`-Instanz (ERSETZT `ableton::Link`, nie parallel) — Ctor `(bpm, peerName)`, Default-Peer-Name "Conduit"; `enableLinkAudio(false)` initial, Audio aktiviert erst das erste Send-Modul
- Neue API [Message Thread]: `enableAudio(bool)` mit Refcount (n aktive Sinks → enabled), `isAudioEnabled()` (RT-safe), `peerName()`/`setPeerName()`, `createSink(name, maxNumSamples)` → opaker Pimpl-Wrapper `LinkClock::Sink` (Design im Header dokumentiert: Link-/asio-Header bleiben in der .cpp, RT-Schreib-API folgt mit dem LinkAudioSendModule)
- `ChannelsChangedCallback` (Link-Thread) wird via `MessageManager::callAsync` + `WeakReference` auf den Message Thread gemarshallt; LinkClock ist nach außen `juce::ChangeBroadcaster`
- Verhaltensneutralität belegt: alle 99 Tests (9637 Assertions) unverändert grün, ASan-Lauf sauber, Transport-UI im Smoke-Test identisch (Tempo/Peers)

**Davor: Capture & Record — Meilenstein komplett (Bausteine 1–7).** Audio-Pendant zu "Capture MIDI": permanenter Pre-Roll, Gate-Detektion mit Auto-Kalibrierung, bedarfsgesteuerte RAM-Ringe, samplegenau alignter BWF-Export bei laufender Aufnahme, Toolbar/Panel-UI. Abschluss-Baustein 7 — Härtung (RT-Audit + Stress-Suite):

- **RT-Audit-Util `Source/Util/RtAllocationGuard`** (wiederverwendbar, auch für bestehende Modul-Pfade): Dev-Builds (`CONDUIT_RT_ALLOCATION_CHECKS=1`, CMake setzt es für Debug beider Targets) ersetzen die globalen operator new/delete; `ScopedRealtimeSection` (thread_local, nestbar) markiert RT-Abschnitte — jede (De-)Allokation darin zählt als Violation (globaler Atomic-Zähler) und hält unter angehängtem Debugger per `__debugbreak` an (bewusst kein jassert: dessen Logging allokiert selbst → Rekursion). Verdrahtet um den Input-Tap in `EngineProcessor::processBlock`; Grenzen dokumentiert (rohes malloc/HeapBlock nicht erfasst — dafür TSan/Review)
- **Device-/Samplerate-Wechsel-Sicherheitsnetz (Entscheidung umgesetzt):** `CaptureService::prepare()` exportiert aktives Material (recording/held) automatisch VOR der Invalidierung — mit der ALTEN Samplerate, die Export-Pins halten den alten Puffersatz bis zum Writer-Abschluss am Leben; danach Clock-Reset + Reallokation. Dokumentiert als EINZIGE Ausnahme von "Verwerfen ohne Auto-Export" (Resize bestätigt der User per Dialog, der Device-Wechsel kommt von außen ohne Rückfrage-Gelegenheit)
- **Stress-Fund + Fix:** Bei VOLLEM Ring und weiterlaufender Aufnahme startete der Export-Leser exakt Kapazität hinter dem Schreib-Cursor — der Überholschutz (Marge = Kapazität/8) brach sofort ALLE Kanäle ab. `enqueueExport` kürzt Snapshots laufender Aufnahmen jetzt auf Kapazität − 2×Marge (1× bleibt Abbruchgrenze, 1× echter Vorsprung ≈ 2 min Echtzeit bei 15-min-Ring); held-Kanäle behalten den vollen Bereich (ihr Ende steht)
- **`Tests/Core/CaptureStressTests.cpp`** (Muster ThreadingStressTests, echte Threads, TSan-Pflicht nach CLAUDE.md 13.4): 16 Kanäle × voller 15-min-Ring × Export bei laufender Aufnahme (Feeder-Thread unter RT-Audit gegen MT-Guard-Ticks, 16 Dateien bitexakt via 32-bit-Float-WAV verifiziert, gemeinsame BWF-TimeReference); Auto-Export-Sicherheitsnetz inkl. Negativ-Kontrolle (keine aktiven Kanäle → kein Export) und Wiederanlauf im frischen Satz; RAM-Wächter räumt NUR gehaltene Kanäle (laufende Aufnahmen bleiben auch bei Dauer-Aushungern unangetastet); Export-Halte-Protokoll (Dekker-Paar) unter Leser-/Freigabe-/Audio-Nebenläufigkeit über 60 Zyklen
- **CI:** neue Quellen in beiden Targets; alle Capture-Tests laufen ohne Audio-Device (Tap wird direkt gefüttert) — TSan/ASan-Presets der CI decken die neuen Threading-Tests automatisch mit ab

**Davor: Capture-System, Baustein 5+6 — Export-Backend + UI (`Source/Core/Capture/`, `Source/UI/`):**
- `CaptureWriter : juce::Thread`: Export NIE im Audio-Thread — Jobs vom MT (Lock + notify erlaubt), Snapshots (start/end) beim Trigger eingefroren, Aufnahme läuft weiter (SPSC-Leser hinter dem Schreib-Cursor)
- ALIGNMENT (Kern-Feature): `exportStart = min(start aller Kanäle)`, `padSamples = start − exportStart` Null-Samples vorweg, bext TimeReference = exportStart für ALLE Dateien → DAW-Import liegt samplegenau übereinander, spätere Spuren beginnen mit Stille
- Format: BWF via `WavAudioFormat` (RF64 ab 4 GB automatisch), Bit-Tiefe aus Settings; Datei vorab in Blöcken allokiert (ENOSPC früh), Header-Flush alle 10 s, Fehler brechen nur den betroffenen Kanal ab (Datei gelöscht, Rest läuft weiter); Dateiname `{timestamp}_{inN|stripName}_{take}.wav`
- Überholschutz dokumentiert: Chunk-Vergabe priorisiert den vollsten Ring, Abbruch unter Sicherheitsmarge (Kapazität/8), `read()` validiert nach dem Kopieren nach
- Export-Halte-Protokoll: `tryBeginExportRead`/`endExportRead` am `CaptureChannel` (Dekker-Paar mit `releaseBarrier`, seq_cst) — Freigaben werden bei aktiven Lesern aufgeschoben (`detachPending`); Satz-Ebene: `BufferSet::exportPins`, ausgemusterte Sätze erst bei Pins == 0 zerstört; Re-Anker aus held nutzt `reanchor()` (nur Atomics, kein attach bei laufendem Leser)
- `CaptureService::exportAll()` [MT], Report per AsyncUpdater auf den MT (`onExportFinished`); `releaseExportedHeldChannels()` für die Nach-Export-Freigabe; TrackSource-Interface so geschnitten, dass ein Live-FIFO (kontinuierliches Multitrack-Recording) später dieselbe Pipeline nutzt
- UI (Baustein 6): `CaptureAllButton` in der Toolbar neben dem Link-Transport (Ring = Status idle/recording/held + Füllstand + Export-Indikator), einklappbares `CapturePanel` — Settings-Controls inkl. Resize-Confirm-Dialog ("Puffergröße ändern löscht alle aktuellen Aufnahmen") plus EINE ZEILE PRO INPUT-KANAL: Status-LED, Mini-Pegel (RMS-Füllung + Peak-Strich) mit Noise-Floor-Marker aus den InputMeter-Atomics, Einzel-Capture-Button 44 px (`CaptureService::exportChannel`, gleiche Pipeline wie exportAll); Kanalzahl folgt dem Device (prepare() feuert ChangeBroadcast, refresh() prüft defensiv); `CaptureToast` ("N Spuren → Ordner", kein AlertWindow); Editor-Timer von 4 auf 15 Hz (EIN Timer, lock-freie Reads, Repaint nur bei Änderung — begründet im EngineEditor-Doc)
- Settings neu: `releaseAfterExport` (Default AUS = behalten); Freigabe läuft IMMER über einen Ok/Cancel-Dialog — der RAM-Puffer wird nie ohne Rückfrage geleert (User-Vorgabe)
- Non-ASCII-UI-Literale als escaped UTF-8 (`String::fromUTF8`) — MSVC liest BOM-lose Quellen als CP1252 (Mojibake im ersten Smoke-Test)
- Tests (`Tests/Core/CaptureWriterTests.cpp`): pure Alignment-Helfer, Padding + BWF-TimeReference im echten File-Roundtrip, Snapshot bei laufender Aufnahme (Producer schreibt parallel weiter, Datei endet exakt bei endPosition), Fehler-Isolation pro Kanal, Überholschutz-Abbruch, Halte-Protokoll (aufgeschobene Freigabe + Barriere)

**Davor: Capture-System, Baustein 4 — Gate-Detektion + AutoCalibrator (`Source/Core/Capture/`):**
- `CaptureGate` pro Kanal (header-only, läuft im Input-Tap): Zustandsmaschine IDLE → OPEN → (Hold abgelaufen) → IDLE; öffnet bei Block-RMS über der effektiven Schwelle, schließt erst nach holdMinutes durchgehend unter Schwelle − 6 dB (Hysterese — Flattern an der Schwelle resettet den Hold-Zähler); Hold zählt in SAMPLES (`computeHoldSamples`: holdMinutes × 60 × sampleRate), nie Wall-Clock
- UI-Status pro Kanal als Atomic (idle/recording/held): recording solange offen, held nach dem Schließen bis Export/RAM-Reclaim — `CaptureService` quittiert Freigaben über `notifyContentDiscarded()`; dB→Gain audio-seitig gecacht (kein pow pro Block)
- AutoCalibrator [Message Thread, 1 Hz über den Guard-Timer]: publiziert `effectiveThreshold = max(Settings-Threshold, NoiseFloor + 12 dB)` in die Kanal-Atomics (`autoCalibrate`), manueller Threshold als Override-Untergrenze; `runAutoCalibration()` public für Tests
- Tap-Verdrahtung: Meter → Gate → (open?) Pre-Roll-Übernahme + Ring-Schreiben; Gates leben unabhängig vom Puffersatz und werden bei Satz-Swap/Invalidate zurückgesetzt; `openGate`/`closeGate` bleiben als Test-Seam public
- Tests (`Tests/Core/CaptureGateTests.cpp`): Zustandsmaschine mit synthetischen Pegelverläufen (Flatter-Test, Hold-Reset, Reopen aus held), pure Helfer, Auto-Kalibrierung hebt die Schwelle über Dauerbrummen (Service-Level), Gate-steuert-Aufnahme end-to-end; bestehende Service-Tests füttern die Rampe jetzt mit 2⁻³⁰ skaliert (unter der Schwelle, Werte bleiben exakt vergleichbar)

**Davor: Capture-System, Baustein 3 — Puffer-Herzstück (`Source/Core/Capture/`):**
- `PreRollBuffer` pro Kanal, IMMER aktiv: positionsadressierter Mono-Ring (Sample p bei `p % capacity`), Allokation nur in `prepare()`; überbrückt die Pool-Latenz nach Gate-Open
- `BufferPool`: RAM erst bei Bedarf — MT besitzt Segmente (HeapBlock, bewusst uninitialisiert, kein Gigabyte-Memset), Audio fordert per atomarem Zähler an, Publikation/Rückgabe über zwei `SpscQueue<float*>`; Vorhalteziel 1 Segment, Surplus wird abgebaut
- `CaptureRingBuffer` pro Kanal: positionsadressierter Aufnahme-Ring (Speicher vom Pool), `startSamplePosition`/`endPosition` atomar — jede Position absolut rekonstruierbar; Leser-Disziplin wie `SpscQueue` (hinter dem Schreib-Cursor)
- `CaptureChannel`: Zustandsmaschine idle/awaitingSegment/recording/held; amortisierte Pre-Roll-Übernahme (Budget 4× Blockgröße, ≥ 2× nötig gegen Verdrängung, Kopieren VOR dem Pre-Roll-Write); `startSamplePosition = clock − preRollLength`; nahtlose Wiedereröffnung aus held, wenn die Gate-Pause im Pre-Roll-Fenster liegt
- `CaptureService`: Puffersatz-Swap via Exchange-Mailbox + Retire-SPSC-Queue (das in Baustein 2 angekündigte Handoff-Protokoll — Reallokation bei laufendem Audio ist jetzt gefahrlos); RAM-Wächter-Timer (200 ms): Pool-Service, Summe committeter Puffer gegen `ramLimitGb`, gibt pro Tick den ältesten GEHALTENEN Kanal frei, `ChangeBroadcaster`-Warnung für die UI; Gate-API `openGate`/`closeGate` als Test-Seam für Baustein 4
- Tests (`Tests/Core/CaptureBufferTests.cpp`): Wraparound (PreRoll + Ring), Übernahme sample-genau gegen synthetische Rampe (inkl. Pool-Brücke, nahtloser Reopen, Neu-Ankern), Amortisierung terminiert im Budget ohne verdrängte Reads, Pool-Handshake mit echten Threads (TSan-Ziel), RAM-Wächter end-to-end

**Davor: Capture-System, Baustein 2 — CaptureSettings + Resize-Policy:**
- `CaptureSettings`: App-Zustand via `juce::ApplicationProperties` (NICHT im ValueTree — loadPreset lässt Capture unberührt, gleiche Trennung wie Link-Tempo); RT-Felder als Atomics [MT→Audio], `ChangeBroadcaster` für die UI
- Felder: bufferMinutes 15 (5–30), preRollSeconds 60 (10–120), thresholdDb −40 (−80…−20), holdMinutes 10 (1–30), autoCalibrate, ramLimitGb 3, exportDirectory, exportBitDepth 24
- Resize-Policy: Kanal aktiv → Wert nicht übernehmen, `PendingResizeRequest`-Callback an die UI (async Confirm), bestätigt → `invalidateAllBuffers()` (kein Auto-Export) + Reallokation; inaktiv → still. Über `ICaptureBufferHost`-Interface getestet (Mock)
- `CaptureService::prepare()` allokiert den Capture-Ring nach Settings (bufferMinutes, gedeckelt durch ramLimitGb); Settings-Atomics werden pro Block im Tap gelesen (Wirkung kommt mit dem Gate)

**Davor: Capture-System, Baustein 1 — Sample-Clock + Input-Metering (`Source/Core/Capture/`):**
- `SampleClock`: globale, lock-free Sample-Position (atomic uint64, release/acquire); tickt am Ende des Input-Taps, Reset bei `prepareToPlay`
- `InputMeter`: Peak/RMS (~50 ms) + Noise-Floor-Schätzer (Minimum-Tracking, ~30-s-Release) für bis zu 64 Kanäle, fixe Arrays, atomics Audio→UI
- `CaptureService`: Input-Tap als ERSTE Operation in `processBlock` (roher Hardware-Input, vor Graph/GraphFader); Marker für Gate, PreRoll-Ring, Capture-Trigger
- Tests: RMS gegen Sinus-Referenz, Noise-Floor-Konvergenz, SampleClock-Monotonie (`Tests/Core/CaptureMeterTests.cpp`)

**Davor: Step-Sequencer, Urzwerg-inspiriert:**
- Engine: 4×16 Steps, CV/Gate ×4, Scale-Quantize über globale Session-Skala (`scaleRoot`/`scaleType` im RootTree, reist pro Block im ClockState)
- UI: 4×16-Grid-Kachel, Scale-Auswahl in der Toolbar, Kontrollleiste für alle Engine-Parameter

## Nächste Kandidaten (offen, Reihenfolge nicht festgelegt)

- **Link Audio Receive, Schritt 2** (CLAUDE.md 7.2 Schritt 3): `LinkAudioReceiveModule` auf der in Schritt 1 gebauten `LinkClock::Source`/Discovery-Infrastruktur — beat-aligned Jitter-Buffer (nie naiv FIFO'en — v1-Drift-Lektion), Int16→Float, zweiphasiges Delete der Source, Kanal-Discovery-UI über den ChannelsChanged-Broadcast, Monitoring-Latenz dokumentieren
- Mixer-Modul (mehrere Inputs) — Capture-Kanal-Buttons wandern dann vom CapturePanel in die Channel-Strips (Export-Dateinamen nutzen seit ChannelNames bereits das Kanal-Label statt `in{N}`)
- Live-FIFO (kontinuierliches Multitrack-Recording) über die bestehende CaptureWriter-Pipeline (TrackSource-Interface liegt bereit)
- Capture-Restpunkte (aus der Baustein-5-Planung): LinkBox-Zielordner (feste Partition vs. USB-Stick-Erkennung "Take mitnehmen" — Writer nimmt das Verzeichnis schon pro Job, nur ein Mount-Watcher fehlt, gehört zum LinkBox-Meilenstein); 24-bit-Packing im RAM (−25 %) erst nach Messung via `getCommittedBytes()` — Float bleibt Default
- ASIO-Schritt für den echten Mehrkanal-Test (ES-3/ES-6): Steinberg-SDK laden (CMake-Hook fertig), `initAudio()` auf > 2 Eingänge erweitern, perspektivisch Audio-Settings-Dialog
- Envelope-Modul (`IClockSlave`)
- CVTunerModule + Kalibrierungs-Workflow (CLAUDE.md 8)
- Touch-Gesten P0: Pinch-Zoom, 10-Finger-Panic (CLAUDE.md 10.1)

## Bewusst verschoben

- **ASIO:** wartet auf manuellen Steinberg-SDK-Download (CMake-Hook `JUCE_ASIO_SDK_PATH` existiert bereits)
- **MIDI 2.0:** bleibt Roadmap; MIDI→CV-Modul startet später mit MIDI 1.0
- **LinkBox-Prototyp:** alter i7-3770K-PC wird als physisches Linux-Testsystem aufgesetzt (JACK/PipeWire, Integrations-/Latenztests — nicht für Sanitizer)

## Arbeitsweise pro Meilenstein

Implementieren → Build + Catch2-Tests → ASan-Lauf → App-Smoke-Test mit Screenshot → Commit einzeln pro Meilenstein → CI beobachten.
