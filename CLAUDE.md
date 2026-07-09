# Conduit Alpha v3 — Claude Code Anweisungen
> C++20 + JUCE 8+  |  Modulares Audio/CV-Instrument  |  Stand: Juli 2026

> Repo: github.com/n0ael/Conduit — umbenannt von `Conduit_Alpha_v2`
> (Juli 2026). Referenzen auf 'Conduit_Alpha_v2', 'alpha v2' oder
> alte Remote-URLs in Commits, Kommentaren oder externen Notizen
> bezeichnen dieses Projekt. Aktueller Stand: Alpha v3.
> Versionsangaben 'v2.x' in der Roadmap sind Feature-Meilensteine,
> keine Repo-Namen.

---

## 1. Rolle & Kontext

Du bist ein C++20 und JUCE (v8+) Audio-Experte. Wir entwickeln "Conduit alpha v3", ein modulares Audio/CV-Instrument mit freiem Node-basiertem Patching — vergleichbar mit einem Hardware-Modular-Synthesizer.

Denke in Architektur und Modulen, bevor du Code schreibst. Liefere Code-Snippets immer sauber getrennt in Header (.h) und Source (.cpp). Bleib technisch, präzise und direkt. Keine Erklärungen, die nicht angefordert wurden.

### 1.1 Subsystem-Dossiers & Rules (Pflichtlektüre)

Detail-Spezifikationen und Lektionen abgeschlossener Subsysteme
liegen in `docs/{Subsystem}.md`, ADRs in `docs/adr/`. Für Arbeiten
am jeweiligen Subsystem sind Dossiers verbindlich wie diese Datei.
Vor JEDER Änderung an einem Subsystem mit Dossier: das Dossier
vollständig lesen; Phase 1 des Auftrags listet die gelesenen
Dossiers. Neue Feature-Spezifikationen wandern nach Abschluss ins
Dossier — in der CLAUDE.md verbleiben nur Invarianten + Verweis.

Die Subsystem-INVARIANTEN leben seit v5.0 als path-scoped Rules in
`.claude/rules/*.md` (ADR 005): sie laden mechanisch, sobald Dateien
des Subsystems gelesen werden — `paths:`-Frontmatter verwenden, NIE
`globs:` (lädt fälschlich unconditional). Beim Anlegen NEUER Dateien
eines Subsystems triggert die Rule erst nach dem ersten Read — die
Phase-1-Inventur (Bestandsdateien lesen) deckt das ab. Querschnitts-
Regeln (v. a. §3 Audio Thread) bleiben bewusst unconditional hier.

---

## 2. Tech-Stack

- Streng C++20, JUCE Framework (NUR native Components, **KEIN ImGui!**), CMake, Ableton Link SDK
- CMake: Nutze `juce_add_plugin` / `juce_add_gui_app`, kein manuelles Linken
- Keine Raw Pointer — JUCE-SmartPointer oder `std::unique_ptr`
- `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` nicht vergessen
- **Kein `AudioProcessorValueTreeState` (APVTS)** — nicht geeignet für dynamische Node-Graphs
- Verschachtelte `Config`-Structs mit In-Class-Defaults nicht direkt als
  Default-Argument (`Klasse(const Config& = {})`) — Clang lehnt das ab.
  Stattdessen delegierender Default-Ctor in der .cpp: `Klasse() : Klasse(Config{}) {}`.
- `juce::Point` braucht `<juce_graphics/juce_graphics.h>` (nicht nur `juce_core`) —
  sonst kaskadierende „Funktion akzeptiert keine 2 Argumente"-Fehler.

---

## 3. Audio Thread

### 3.1 Lock-free & Allocation-free (Non-Negotiable)

```cpp
// VERBOTEN im Audio-Callback:
// - Heap-Allokationen (new / malloc)
// - Mutex-Locks (std::mutex::lock)
// - UI-State-Reads ohne lock-free Queue
// - String-Operationen
// - OS-Calls (file I/O, logging)
// - RT-inkompatible Kernel-Calls (PREEMPT_RT)

// PFLICHT:
// - SpscQueue (Source/Util/SpscQueue.h) oder std::atomic<> für
//   Parameter-Updates
// - juce::AbstractFifo NICHT verwenden — SpscQueue ist der einzige
//   Inter-Thread-Queue-Baustein (Catch2-getestet, TSan-abgedeckt)
// - SPSC-Ringbuffer zwischen UI-Thread und Audio-Thread

// PFLICHT (Ergänzung):
// - Zufall im Audio-Thread NUR via inline LCG (state = 1664525*state +
//   1013904223), nie rand()/<random>-Engines — heap- und lock-frei,
//   deterministisch pro Seed (IStochastic).
// - Zeitbasen: musikalische Zeit aus dem ClockState des Blocks,
//   absolute Zeit in SAMPLES (SampleClock). Keine Wall-Clock-/ns-Mathematik
//   im Audio-Thread mischen (v1-Lektion: ns-Basis funktioniert, aber
//   Mischbetrieb erzeugt Drift- und Rundungsfehler an den Nahtstellen).
```

### 3.2 Latenz-Ziele

| Parameter | Zielwert | Fallback |
|---|---|---|
| Buffer Size | 32–128 Samples (Warnung erst > 256; Untergrenze beurteilt der XRun-Zähler live, keine starre Schwelle) | — |
| Sample Rate | 48 000 Hz | 44 100 Hz |
| Audio-Callback RTL | < 2 ms intern | — |
| Glass-to-Sound | < 10 ms gesamt | — |

---

## 4. Modul-Hierarchie

### 4.1 Basisklassen

```
ConduitModule                    (abstrakte Basis, erbt AudioProcessor)
├── GeneratorModule              LFO, Envelope, Sequencer, MIDI→CV
├── ProcessorModule             
│    └── PluginModule            CLAP-Host wrapper (v2.x)
├── IOModule
│    ├── HardwareIOModule        ES-3, ES-5, ESX-8GT, ESX-8CV
│    └── NetworkIOModule         OSC ↔ Ableton M4L
├── AnalysisModule               Scope, Tuner, FFT, CVTunerModule
└── UtilityModule                Mixer, Attenuator, DC Block, Math, Offset
```

### 4.2 Mixin-Interfaces & Thread-Ownership

Jedes Interface definiert explizit auf welchem Thread seine Methoden aufgerufen werden:

```
IClockSource      — erzeugt Takt          → Audio Thread
IClockSlave       — konsumiert Takt        → Audio Thread
ISidechain        — zweiter Input-Bus      → Audio Thread
IStochastic       — Zufalls-Parameter      → Audio Thread (Seed-Updates: Message Thread)
IPolyphonic       — mehrere Stimmen        → Audio Thread (vorbereitet, v2.x)
ITouchMacro       — Touch-Verhalten        → Message Thread (UI-Events)
```

Methoden die Thread-Grenzen überschreiten müssen SPSC-Queue oder `std::atomic` nutzen.
Niemals Interface-Methoden vom falschen Thread aufrufen.

### 4.3 Beispiel: Mehrere Interfaces

`class StepSequencer : public GeneratorModule, public IClockSlave, public IStochastic {};`
— primärer CV/Trigger-Output via Basisklasse, Takt + Zufall als Mixins [Audio Thread].

### 4.4 Pflicht-Methoden jedes Moduls

- `createState()` — lazy, VOR `addNode()` aufgerufen, **nicht** im Konstruktor
- `getModuleId()` — named_id für OSC-Pfad, z.B. `neutron_filter`
- `getModuleDisplayName()` — lokalisierter UI-Name, getrennt von moduleId
- `getType()` — ModuleType enum für GraphManager
- `getStateVersion()` — int für Serialisierungs-Versioning (Rückwärtskompatibilität)

### 4.5 Launch-Quantisierung (Pattern für IClockSlave-Module)

Quantisierte Start/Stop/Reset-Aktionen folgen dem Ableton-Grid; Grid-Enum
zentral und app-weit (`Source/Core/LaunchQuantization.h`), damit alle
Module identisch quantisieren. Kanonisches Muster (atomare Pending-Flags,
sample-genaue Grid-Überquerung): Rule `transport`.

---

### 4.6 FX-Chassis-Standard (ProcessorModule) — verbindlich für ALLE FX-Module

Jedes Audio-FX-Modul erbt `ProcessorModule` und implementiert NUR
`prepareCore()`/`processCore()` — Chassis-Bestandteile NIE nachbauen.
Invarianten: Rule `fx-chassis`; Spezifikation: **docs/FxChassis.md —
Pflichtlektüre vor jeder FX-Arbeit.**

---

## 5. Patch-Engine (Glitch-freier Graph-Swap)

Detail-Abläufe + Code-Muster: **docs/PatchEngine.md — Pflichtlektüre vor
Arbeiten an GraphManager, Graph-Swap, Delete-Pfad oder Preset-System.**

### 5.1 Architektur

- `juce::AudioProcessorGraph` ist die DSP-Engine; jedes Modul ist ein
  eigenständiger `AudioProcessor` im Graph
- `ValueTree` + `UndoManager` für Zustand, Serialisierung, Undo/Redo, UI-Binding
- Graph-Mutationen **NUR auf Message Thread**; alle patchbaren Aktionen
  (add, remove, connect, disconnect) gehen durch `UndoManager`

### 5.2 Modul hinzufügen / Kabel umstecken (4-Schritt Ablauf)

Async Prepare (manuelles `prepareToPlay()` VOR dem Swap; Fehler → `nodeError`,
Modul nicht aufnehmen) → DSP Fade-Out [Audio Thread] → Topologie-Swap
[Message Thread via `AsyncUpdater`, **kein Busy-Poll, kein Timer** —
self-re-dispatch bis `fadeComplete`] → DSP Fade-In. JUCE rebuildet den
Rendering-Plan auf Stille — kein Knacksen.

### 5.3 Modul löschen (Zweiphasiges Delete — Zombie-UI-Schutz)

**Regel: UI-Component hält niemals einen Pointer auf den Processor — nur auf
den ValueTree-Subtree.** Phase 1 [Message Thread]: `nodeState → Deleting`,
UI entkoppelt sich via Listener, `OscController` cached `moduleId` JETZT und
deregistriert sofort. Phase 2 [Message Thread, nächster Frame via
`VBlankAttachment`, erst wenn kein UI-Listener mehr auf dem Subtree]:
`removeNode()`, Subtree-Entfernung (UndoManager), Destruktion.
`nodeState` enum: Active | FadingOut | FadingIn | Deleting.

### 5.4 Preset-System (Speichern / Laden)

Snapshot = `rootTree.createCopy()` → XML-Datei; Speichern nur wenn kein
`isDirty`-Flag gesetzt (6.1). Laden rebuildet den Graph aus dem Tree,
CalibrationProfiles werden mitgeladen und sofort angewendet.

### 5.5 Batch-Coalescing (Undo / Bulk-Delete)

`GraphManager` (erbt `AsyncUpdater`) sammelt alle ValueTree-Änderungen eines
Frames und führt EINEN gemeinsamen Fade-Zyklus/Graph-Swap für den gesamten
Delta aus — nie einen pro Änderung. Gilt für Undo, Redo, Preset-Load,
Bulk-Delete, Copy-Paste.

### 5.6 Regeln

- `SmoothedValue`-Rampzeit: 5ms default, konfigurierbar pro Node;
  `fadeComplete` ist `std::atomic<bool>` — kein Mutex
- Kein `new`/`malloc` während des gesamten Ablaufs
- `prepareToPlay()`-Fehler → `nodeError`-Property, nie ignorieren

---

## 6. Datenmodell & Threading

- `juce::ValueTree` ist die Single Source of Truth für **Zustand und Serialisierung**
- `AudioProcessor` besitzt den Root-ValueTree — Editor ist read/listen-only
- ValueTree-Mutationen **NUR auf dem Message Thread**
- `juce::UndoManager` für alle patchbaren ValueTree-Mutationen
- Kein APVTS — nicht geeignet für dynamische Node-Graphs
- `createState()` erzeugt Subtree mit Properties, Defaults und Ranges
- `GraphManager` hängt Subtree via `addChild()` in Root-Tree (nie das Modul selbst)
- `getStateVersion()` — jeder Subtree trägt eine Versionsnummer für Migration
- **Session-transiente IDs nie serialisieren:** Connections/Referenzen auf
  Objekte, deren IDs pro Session neu vergeben werden, dürfen nicht in
  Presets landen (v1-Lektion: gespeicherte Encoder-Verbindungen luden als
  Phantom-Connections). In v2 sind Node-Uuids persistent — die Regel gilt
  für alles Künftige mit Laufzeit-IDs (z. B. Link-Audio-ChannelIds von
  Peers: discoverbar, nie Teil des Patches).

### 6.1 OSC Dual-State (Echtzeit vs. UI-Konsistenz)

ValueTree ist Single Source of Truth für Zustand — aber **nicht** für
Echtzeit-Parameter-Updates. OSC-Changes laufen parallel auf zwei Pfaden
[Network Thread]: SPSC-Queue → Audio Thread (sofort, lock-free, < 1 ms)
UND `MessageManager::callAsync` → ValueTree (UI + Serialisierung, ~1 Frame).
**Serialisierungs-Regel:** `isDirty` (`std::atomic<bool>`) guarded
Preset-Save und Undo-Snapshot — bei `true` einen callAsync-Zyklus warten,
dann serialisieren; so gehen keine OSC-Werte beim Speichern verloren.
Code-Muster + Latenz-Tabelle: docs/DataModel.md.

### 6.2 ValueTree Schema

Vollständiges Schema (RootTree-Properties, Nodes[], Parameters[],
Connections[], CalibrationProfiles[], Session-Skala/`followAbleton`):
**docs/DataModel.md — Pflichtlektüre vor Schema-Änderungen.** Invarianten:

- Jeder Node-Subtree trägt `stateVersion` (Migration) und `nodeState`
  (Active | FadingOut | FadingIn | Deleting).
- Reservierte moduleIds `audio_input` / `audio_output`: der GraphManager
  mappt sie auf die Audio-I/O-Prozessoren des EngineProcessor — keine
  Factory-Materialisierung, nicht löschbar.
- `remoteId` = dokumentierte Ausnahme zur Laufzeit-ID-Regel oben (7.4).
- Session-Skala (`scaleRoot`/`scaleType`, 25 Ableton-Presets als 12-Bit-Maske,
  Index-Mapping statt Nearest-Note) und `globalSwing` reisen pro Block im
  ClockState zu den Modulen.

---

## 7. OSC-Integration

- OSC-Receive auf dediziertem Netzwerk-Thread (nicht Message Thread)
- Parameter-Updates via SPSC-Queue → Audio Thread (siehe 6.1 Dual-State)
- **Pfad-Schema:** `/conduit/{type}/{named_id}/value`
- Named IDs persistent über Ableton-Neustarts — kein Drag-and-Drop-Assignment

### 7.1 Auto-Registration via ValueTree::Listener

`OscController` registriert/deregistriert OSC-Adressen automatisch über
den Root-Tree-Listener; Deregistrierung in Delete-Phase 1. DSP-Module
wissen nichts von OSC. Details: Rule `osc-remote`.

### 7.2 Link Audio (Audio in der Link-Session)

Send + Clock + Receive implementiert (Receive: beat-aligntes
Latenzfenster `latency_ms`; Live-Feldtest offen 08.07.2026).
Invarianten (LinkAudio ERSETZT Link, int16+TPDF, WeakReference-Pflicht):
Rule `linkaudio`; Spezifikation + Lektionen: **docs/LinkAudio.md —
Pflichtlektüre vor jeder LinkAudio-Arbeit.**

### 7.3 OSC-Send (Parameter-Feedback an Clients)

Snapshot-Diff @ 30 Hz auf dem Message Thread, Default-Port 9001 (NICHT
9000). Invarianten: Rule `osc-remote`; Details: **docs/OscSend.md**.

### 7.4 Max4Live-Announce (Remote-Module)

`remoteId` = dokumentierte Ausnahme zur Laufzeit-ID-Regel (§6).
Invarianten: Rule `osc-remote`; Details: **docs/M4LAnnounce.md**.

---

## 8. CV-Hardware-Kalibrierung

DC-coupled Interfaces (ES-3, ESX-8CV etc.) haben hardware-spezifische DC-Offsets und
Gain-Abweichungen. `0.0f` digital ≠ `0.000V` analog → Out-of-Tune bei 1V/Oct.

### 8.0 Interne Spannungs-Konvention

- Intern gilt: float ±1.0 == Full Scale des Interfaces. Bei ±10-V-Hardware
  (ES-Serie) entspricht 1 V also 0.1f; Eurorack-Gate-High (+5 V) = 0.5f.
- Module rechnen IMMER in dieser normalisierten Skala; die Umrechnung in
  echte Volt passiert ausschließlich im HardwareIOModule über das
  CalibrationProfile (dcOffset/gainTrim) plus `fullScaleVolts` pro
  Interface (neues Profil-Feld, Default 10.0).
- UI zeigt Volt an, speichert normalisiert.

### 8.1 CalibrationProfile (per Interface)

`interfaceId` (exakter Device-Name, primärer Key) + `interfaceIdPrefix`
(Fallback ohne Suffix wie " (2)") + `dcOffset` + `gainTrim`; im
HardwareIOModule allocation-free angewendet:
`(raw + dcOffset) * gainTrim`. Matching bei USB-Reconnect: exakt → Prefix →
kein Match = Neutral-Profil + UI-Warnung. Profile sind kanalspezifisch,
persistent im ValueTree, user-adjustierbar. Details: docs/Calibration.md.

### 8.2 CVTunerModule (AnalysisModule)

Natives Kalibrierungswerkzeug (Referenz-CV ausgeben → Rückweg via ES-6
messen → dcOffset/gainTrim berechnen → Profil in ValueTree schreiben,
sofort aktiv, wiederholbar pro Kanal). Schreibt NUR CalibrationProfiles,
nie in den Audio-Pfad; Messung auf separatem Analyse-Thread.
Ablauf: docs/Calibration.md.

### 8.3 Latenz-Trim für CV-Ausgänge

Pro CV-Ausgangskanal `shiftMs` (±50 ms) plus globales `globalShiftMs`,
beide als Beat-Offset im Audio-Thread eingerechnet — gehört ins
CalibrationProfile bzw. den Kanal-State, user-adjustierbar (docs/Calibration.md).

---

## 9. Plattformen & Backends

| Plattform | Priorität | Audio-Backend | Besonderheit |
|---|---|---|---|
| Windows | Primary | ASIO | Dev/DAW |
| macOS | Primary | CoreAudio | Dev/DAW, 32 Samples problemlos |
| Linux Desktop | Secondary | JACK + FFADO | FireWire-Interfaces |
| Linux Kiosk (LinkBox) | Secondary | JACK / PipeWire | PREEMPT_RT, Fullscreen |
| iOS | Optional / Bonus | CoreAudio Remote I/O | Touch-first |

**Plattform-Scope-Regel:** Kein plattformspezifischer DSP- oder UI-Code.
Plattform-spezifisches Setup in `initAudio()` und CMake ist explizit erlaubt.

### 9.1 macOS CoreAudio

Setup-Details (Buffer-Size-Verhalten, `audioSetupWarning`-Property,
defensives `initAudio()`): docs/Build.md.

### 9.2 Linux Kiosk-Mode (LinkBox)

Fullscreen/borderless, Cursor-Handling, PREEMPT_RT-Regeln,
Touch-Kalibrierung: docs/Build.md.

---

## 10. UI & Input

### 10.0 Push-3-Design-System (Stand Juli 2026)

Design-System (PushLookAndFeel, PushIcons/PushTiles, Pages) + UI-Regeln
im Detail: Rule `ui-design` (lädt bei Arbeit unter `Source/UI/`).
Querschnitts-Kern:

- Touch-first: minimale Touch-Target-Größe 44px, vollständig
  Mouse/Keyboard-kompatibel — kein Touch-only Code.
- **Schrift wird NIE horizontal gestaucht (User-Regel 07/2026):**
  Schriftgröße reduzieren oder Text kürzen — niemals quetschen
  (minimumHorizontalScale = 1.0, Details Rule `ui-design`).
- UI-Components binden NUR an den ValueTree-Subtree, nie an den
  Processor (5.3); Animationen via `VBlankAttachment`, kein Blocking
  in `paint()`.

Subsystem-Regeln + Spezifikationen (je eigene Rule + Dossier):
**TransportBar/Metronom** → Rule `transport`, docs/Transport.md ·
**Looper** (Engine-Level, kein Graph) → Rule `looper`, docs/Looper.md ·
**Grid-Touch-Controller Ω** → Rule `grid`, docs/Grid.md.

### 10.1 Touch-Gesten

Gesten-Tabelle (Sonne/Mond, Pinch, Rotate, 3-Finger-Tap, Long Press):
Rule `ui-design`.

---

## 11. Feature-Roadmap (Scope-Referenz)

Erledigte v2.0-Features stehen nicht mehr hier — sie sind in den Dossiers
dokumentiert: Link Audio Send, Transport-Header, FX-Chassis, Looper inkl.
Vollausbau, OSC-Send, M4L-Announce (+ Max-Testdevice ConduitLFO), Grid M1.

| Feature | Version | Notiz |
|---|---|---|
| Link Audio Receive | v2.x | implementiert 08.07.2026 (docs/LinkAudio.md) — Live-Feldtest offen |
| I/O-Konsolidierung (User-Idee 08.07.2026) | v2.x | audio_input/audio_output starten stereo, „+" fügt Hardware- ODER Link-Kanäle hinzu (ein Modul für alle Ins, eins für alle Outs); InputSendButtons entfallen dann; Receive-/Send-Motoren bleiben die Basis |
| Gate, EQ, Compressor | v2.0 | ProcessorModule, ISidechain |
| CVTunerModule | v2.0 | AnalysisModule, CalibrationProfile |
| CLAP-Hosting | v2.x | PluginModule wraps AudioPluginInstance |
| IPolyphonic | v2.x | Interface vorbereitet, noch nicht implementiert |
| VST3-Hosting | v3.0+ | Steinberg-Lizenz, nach CLAP |
| Cardinal/VCV Integration | v3.0+ | Touch-native Modular UI |
| Expert-Sleepers-Encoder (ES-5/ES-4(0)/8CV/8GT) | v2.x | v1-Port vorhanden (EncoderEngines.hpp, MIT/VCV) — HardwareIOModule-Grundstein |
| Euclid-/Turing-Module | v2.x | v1-Engines als Referenz (Launch-Quant, parametrischer Swing, Scale-Quantize) |
| Mixer-Page | v2.x | ∥∥-Icon, Channel-Strips (Capture-Buttons wandern dorthin) |
| Grid-Page weitere Meilensteine | Ω-Icon | Meilensteinleiter: docs/Grid.md |
| Clip-Page (Fugue-Machine-Sequencer) | v2.x | ▷▭-Icon, immer aktiv, CV- UND MIDI-Ziele; Slot 2 vorerst an TouchLive abgegeben (09.07.2026) |
| TouchLive-Page (Ableton-Live-Remote) | v2.x | M1a–M1c erledigt (Script + Client/Modell + GRID/MIXER-Page auf Slot 2), Meilensteinleiter: docs/TouchLive.md |
| Capture-Netzwerk-Share (Exports für entferntes Ableton) | v2.x | HTTP-Bereitstellung der Capture-Dateien |

---

## 12. Out-of-Scope (bewusst ausgeschlossen)

- ImGui-basierte Conduit v1-Architektur
- M4L-Patchbay-Integration (Glymma-Scope)
- Hardware-Spezifikation LinkBox Mini / Pro
- Rechtliche Struktur / UG-Gründung / Pricing
- Plattformspezifischer DSP- oder UI-Code (Setup-Code in `initAudio()` erlaubt)

---

## 13. Tooling & Technische Guardrails

### 13.1 Compiler & Abhängigkeiten

- **C++ Standard:** Strikt C++20 — `set(CMAKE_CXX_STANDARD 20)`, `set(CMAKE_CXX_STANDARD_REQUIRED ON)`
- **JUCE Version:** Minimum JUCE 8.0.0, via CMake `FetchContent` (kein Submodule, kein System-Install)
- **Ableton Link:** via `FetchContent` (header-only)
- **Warnungen als Fehler:** `-Wall -Wextra -Werror` (GCC/Clang), `/W4 /WX` (MSVC)

### 13.2 Preprocessor Defines (RT Safety Guardrails)

`JUCE_MODAL_LOOPS_PERMITTED=0` (keine blockierenden Modal-Loops im Message
Thread), `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`. Plattform-Defines NIE
global setzen: `JUCE_USE_CORE_AUDIO=1` nur APPLE; `JUCE_ASIO=1` nur WIN32
und nur mit Steinberg ASIO SDK (separater Download + Lizenz, Pfad via
`JUCE_ASIO_SDK_PATH`). CMake-Snippet: docs/Build.md.

### 13.3 Quick Start & Build-Workflow

- Windows (VS 2026 installiert, KEIN VS 2022):
  `cmake -B build -G "Visual Studio 18 2026" -A x64`, dann
  `cmake --build build --config Debug`; Test = Standalone-Target aus `build/`.
- Sanitizer-Presets (13.4): `cmake --preset asan` (MSVC, läuft lokal unter
  Windows) / `tsan` (Clang, NUR Linux/macOS/WSL). TSan + ASan laufen
  außerdem in GitHub Actions (Ubuntu) bei jedem Push auf master
  (`.github/workflows/ci.yml`). Ninja-Variante + Details: docs/Build.md.

### 13.4 Testing & Validierung

- **Framework:** Catch2 v3 via `FetchContent`, eigenes `ConduitTests`-Target
- **Pflicht-Unit-Tests vor Integration:** SPSC-Queues, ValueTree-Serialisierung/-Migration,
  Graph-Topologie-Änderungen (Fade-Zyklen, Batch-Coalescing), CalibrationProfile-Matching
- **ThreadSanitizer:** Eigene CMake-Preset/Config mit `-fsanitize=thread` (Clang) —
  Pflicht-Lauf für allen Code, der Thread-Grenzen überschreitet (SPSC, atomics, AsyncUpdater)
- **AddressSanitizer:** `-fsanitize=address` für Graph-Swap- und Delete-Pfade (Zombie-UI, Use-after-free)
- TSan/ASan-Builds laufen mit Dummy-Audio-Device — kein ASIO nötig

### 13.5 Projektstruktur

`Source/{Core,Modules,Interfaces,UI,Util}` + `Tests/` (spiegelt
Source/-Struktur), je Modul ein .h/.cpp-Paar; UI nur ValueTree-gebunden.

---

## 14. ADRs (Architecture Decision Records)

ADRs liegen in `docs/adr/` (append-only, wortgleich ausgelagert).
Index:
- 001 — SpscQueue als einziger Inter-Thread-Queue-Baustein + Repo-Rename
- 002 — Grid-Page als Touch-Controller-Baukasten
- 003 — Grid-Voice-Ausgabe: Quelle → Voice-Modell → austauschbare Sinks
- 004 — Subsystem-Dossiers unter docs/ (+ Descope 10-Finger-Panic)
- 005 — Subsystem-Invarianten als path-scoped Rules (.claude/rules/)

---

*Conduit Alpha v3 — Claude Code Instructions v5.0  |  Juli 2026*
