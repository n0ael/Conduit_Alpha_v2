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

### 1.1 Subsystem-Dossiers (Pflichtlektüre)

Detail-Spezifikationen und Lektionen abgeschlossener Subsysteme
liegen in `docs/{Subsystem}.md`, ADRs in `docs/adr/`. Für Arbeiten
am jeweiligen Subsystem sind Dossiers verbindlich wie diese Datei.
Vor JEDER Änderung an einem Subsystem mit Dossier: das Dossier
vollständig lesen; Phase 1 des Auftrags listet die gelesenen
Dossiers. Neue Feature-Spezifikationen wandern nach Abschluss ins
Dossier — in der CLAUDE.md verbleiben nur Invarianten + Verweis.

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

```cpp
class StepSequencer
    : public GeneratorModule   // primärer CV/Trigger-Output
    , public IClockSlave       // konsumiert externen Takt  [Audio Thread]
    , public IStochastic       // Randomization             [Audio Thread]
{};
```

### 4.4 Pflicht-Methoden jedes Moduls

- `createState()` — lazy, VOR `addNode()` aufgerufen, **nicht** im Konstruktor
- `getModuleId()` — named_id für OSC-Pfad, z.B. `neutron_filter`
- `getModuleDisplayName()` — lokalisierter UI-Name, getrennt von moduleId
- `getType()` — ModuleType enum für GraphManager
- `getStateVersion()` — int für Serialisierungs-Versioning (Rückwärtskompatibilität)

### 4.5 Launch-Quantisierung (Pattern für IClockSlave-Module)

Quantisierte Start/Stop/Reset-Aktionen folgen dem Ableton-Grid
(None/8/4/2/1 Bar, 1/2 … 1/32). Kanonisches Muster (v1-erprobt):

- UI/OSC setzt `startPending`/`stopPending` (std::atomic<bool>, release).
- Audio-Thread erkennt Grid-Überquerung pro Block sample-genau:
  `floor(beat / qBeats) > floor(prevBeat / qBeats)` → Aktion ausführen,
  Flag per `exchange(false, acq_rel)` konsumieren.
- qBeats == 0 → sofort am Blockanfang.
- Grid-Größen und Namen zentral definieren (ein Enum, app-weit), damit
  Sequencer/Euclid/Clock-Module identisch quantisieren.

---

### 4.6 FX-Chassis-Standard (ProcessorModule) — verbindlich für ALLE FX-Module

- Jedes Audio-FX-Modul erbt `ProcessorModule` und implementiert NUR
  `prepareCore()`/`processCore()` (Stereo-Sicht, Kanäle 0..1);
  `prepareToPlay`/`processBlock`/`appendParametersTo`/
  `getParameterTarget` sind final — NIE überschreiben.
- Kanal-Layout FEST: Audio 0..1, CV 2..N
  (`ChassisSchema::cvChannelForParam`); Parameter-`role`:
  "dsp" | "chassis" | "cvAmount".
- Chassis-Bestandteile (Gains, Meter, Link-Send-Tap,
  CV-Richtungsmodell, Control-Linking, Schema-/Migrations-Regeln,
  `FxModulePanel`) NIE nachbauen — Spezifikation:
  **docs/FxChassis.md — Pflichtlektüre vor jeder FX-Arbeit.**

---

## 5. Patch-Engine (Glitch-freier Graph-Swap)

### 5.1 Architektur

- `juce::AudioProcessorGraph` ist die DSP-Engine
- Jedes Modul ist ein eigenständiger `AudioProcessor` im Graph
- `ValueTree` + `UndoManager` für Zustand, Serialisierung, Undo/Redo, UI-Binding
- Graph-Mutationen (`addNode` / `removeConnection`) **NUR auf Message Thread**
- Alle patchbaren Aktionen (add, remove, connect, disconnect) gehen durch `UndoManager`

### 5.2 Modul hinzufügen / Kabel umstecken (4-Schritt Ablauf)

**Schritt 1 — Async Prepare [Message/Background Thread]**
- Modul instanziieren, manuell `prepareToPlay()` aufrufen
- Schlägt `prepareToPlay()` fehl: Fehler in ValueTree-Property `nodeError` speichern,
  Modul nicht in Graph aufnehmen, UI zeigt Fehlerzustand — kein Crash, kein Retry-Loop
- Speicherintensive Allokationen (Delay-Buffer etc.) VOR dem Swap abschließen

**Schritt 2 — DSP Fade-Out [Audio Thread]**
- Graph-Topologie wird noch NICHT geändert
- Message Thread: `state.store(FadingOut)` via `std::atomic`
- `SmoothedValue` rampt Buffer → `0.0f` über ~5ms
- Bei `getCurrentValue() == 0.0f`: `fadeComplete.store(true)`

**Schritt 3 — Topologie-Swap [Message Thread via AsyncUpdater]**
- **Kein Busy-Poll, kein Timer** — `juce::AsyncUpdater::triggerAsyncUpdate()` aufrufen
- In `handleAsyncUpdate()`: prüfe `fadeComplete`
  - `false` → `triggerAsyncUpdate()` erneut (self-re-dispatch, kein UI-Block)
  - `true`  → `addConnection()` / `removeConnection()` ausführen
- JUCE rebuildet Rendering-Plan auf Stille — kein Knacksen

**Schritt 4 — DSP Fade-In [Audio Thread]**
- Message Thread: `state.store(FadingIn)`
- `SmoothedValue` rampt Buffer `0.0f` → `1.0f`

### 5.3 Modul löschen (Zweiphasiges Delete — Zombie-UI-Schutz)

**Regel: UI-Component hält niemals einen Pointer auf den Processor — nur auf den ValueTree-Subtree.**

**Phase 1 [Message Thread] — UI entkoppeln:**
- ValueTree-Property `nodeState` → `Deleting` setzen
- `OscController` cached `moduleId` dieses Nodes **jetzt** (nicht erst bei `valueTreeChildRemoved`)
- UI-Component reagiert via Listener: stoppt Rendering, deregistriert alle Listener,
  gibt ValueTree-Referenz frei
- `OscController` deregistriert OSC-Adressen sofort via gecachter `moduleId`

**Phase 2 [Message Thread, nächster Frame] — Objekt zerstören:**
- `juce::VBlankAttachment` stellt sicher dass UI-Render-Zyklus abgeschlossen ist
- `removeNode()` aus `AudioProcessorGraph`
- ValueTree-Subtree entfernen (via `UndoManager` für Undo-Fähigkeit)
- Objekt destrukturieren

```cpp
// nodeState enum: Active | FadingOut | FadingIn | Deleting
// Phase 2 startet erst wenn nodeState == Deleting UND kein UI-Component
// mehr einen Listener auf diesem Subtree hält.
// OscController liest moduleId in Phase 1, nicht aus valueTreeChildRemoved-Callback.
```

### 5.4 Preset-System (Speichern / Laden)

```cpp
// Speichern: nur wenn kein dirty-Flag gesetzt (siehe 6.1)
juce::ValueTree snapshot = rootTree.createCopy();
juce::XmlElement* xml = snapshot.createXml();
xml->writeToFile(presetFile, {});

// Laden:
juce::XmlElement xml = juce::XmlDocument::parse(presetFile);
juce::ValueTree loaded = juce::ValueTree::fromXml(*xml);
// → Graph-Manager rebuildet AudioProcessorGraph aus geladenem Tree
// → CalibrationProfiles werden mitgeladen und sofort angewendet
```

### 5.5 Batch-Coalescing (Undo / Bulk-Delete)

Wenn der `UndoManager` einen Batch-Undo ausführt (z.B. 5 Module + 20 Kabel in einem Frame),
darf der `GraphManager` nicht 25 separate Fade-Out/Fade-In-Zyklen triggern.

**Regel:** `GraphManager` sammelt alle ValueTree-Änderungen eines Frames via `AsyncUpdater`,
führt dann einen einzigen gemeinsamen Graph-Swap durch:

```cpp
// GraphManager erbt juce::AsyncUpdater
// valueTreeChanged() → markDirty(), triggerAsyncUpdate()
// handleAsyncUpdate() → einen Fade-Zyklus für den gesamten Delta
```

Dies gilt für: Undo, Redo, Preset-Load, Bulk-Delete, Copy-Paste.

### 5.6 Regeln

- `SmoothedValue`-Rampzeit: 5ms default, konfigurierbar pro Node
- `fadeComplete` ist `std::atomic<bool>` — kein Mutex
- Kein `new`/`malloc` während des gesamten Ablaufs
- `prepareToPlay()`-Fehler → `nodeError`-Property, nie ignorieren
- Mehrere gleichzeitige Graph-Änderungen immer zu einem einzigen Swap coalescing

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

ValueTree ist Single Source of Truth für Zustand — aber **nicht** für Echtzeit-Parameter-Updates.
OSC-Parameter-Changes laufen parallel auf zwei Pfaden:

```
OSC → [Network Thread]
         │
         ├─► SPSC-Queue ──────────────► Audio Thread   (sofort, lock-free, < 1ms)
         │
         └─► MessageManager::callAsync ► ValueTree      (UI folgt nach, ~1 Frame)
                                          setzt isDirty = true
```

```cpp
// OscController::handleMessage() [Network Thread]
void onOscMessage(const juce::OSCMessage& msg) {
    // 1. Sofort in Audio Thread (lock-free)
    audioQueue.push({ parameterId, value });

    // 2. Async in ValueTree — setzt dirty flag für Serialisierung
    juce::MessageManager::callAsync([this, parameterId, value] {
        rootTree.getChildWithProperty("id", parameterId)
                .setProperty("value", value, nullptr);
        isDirty.store(false);  // ValueTree ist jetzt aktuell
    });

    isDirty.store(true);  // Serialisierung muss warten
}
```

**Serialisierungs-Regel:** Preset-Save und Undo-Snapshot prüfen `isDirty`.
Wenn `true`: einen `callAsync`-Zyklus warten, dann serialisieren.
Dadurch gehen keine OSC-Werte beim Speichern verloren.

| Pfad | Zweck | Latenz-Anforderung |
|---|---|---|
| SPSC-Queue | DSP-Parameter | < 1ms |
| ValueTree async | UI + Serialisierung | 10–50ms akzeptabel |
| isDirty flag | Serialisierungs-Guard | `std::atomic<bool>` |

### 6.2 ValueTree Schema

```
RootTree
  ├── scaleRoot / scaleType   (globale Session-Skala: 0–11 + chromatic/major/minor/pentatonic;
  │                            reist pro Block im ClockState zu den Modulen)
  ├── globalSwing             (globaler Session-Swing 0..0.75, Header-Regler; reist im
  │                            ClockState — IClockSlaves mit lokalem Swing 0 folgen ihm,
  │                            lokaler Swing > 0 überschreibt pro Modul, 4.5)
  └── Nodes[]
       ├── nodeId         (juce::Uuid)
       ├── type           (ModuleType enum als String)
       ├── factoryId      (unveränderlicher Factory-Schlüssel, z.B. "attenuator")
       ├── moduleId       (named_id, user-editierbar, eindeutig — z.B. "neutron_filter")
       ├── stateVersion   (int, für Migration)
       ├── nodeState      (Active | FadingOut | FadingIn | Deleting)
       ├── nodeError      (String, leer wenn kein Fehler)
       ├── position       (x, y für UI)
       ├── numInputChannels / numOutputChannels   (int, für die Port-UI)
       ├── remoteId       (optional: Announce-Bindung 7.4 — persistente
       │                   Gegenstelle im Live-Set, dokumentierte Ausnahme
       │                   zur Laufzeit-ID-Regel oben)
       ├── tintColour     (optional: Track-Farbe 0x00RRGGBB, folgt Re-Announce)
       ├── linkSendEnabled (bool: FX-Chassis-Send-Tap am Ausgang, 4.6)
       └── Parameters[]
            ├── id, value, min, max, default
            ├── role       ("dsp"|"chassis"|"cvAmount" — FX-Chassis 4.6)
            ├── userMin, userMax, uiHidden, curve        (Dev-Modus, optional)
            └── linkSource, linkAmount, linkCurve        (Control-Link, optional)
  └── Connections[]
       ├── sourceNodeId, sourceChannel
       └── destNodeId,   destChannel

# Reservierte moduleIds: "audio_input" / "audio_output" — Tree-Nodes, die der
# GraphManager auf die Audio-I/O-Prozessoren des EngineProcessor mappt
# (keine Factory-Materialisierung, nicht löschbar, Graph-Node bleibt erhalten)
  └── CalibrationProfiles[]
       ├── interfaceId        (Hardware-Device-Name, primärer Key)
       ├── interfaceIdPrefix  (Prefix ohne Suffix wie " (2)", Fallback-Key)
       ├── dcOffset           (float)
       └── gainTrim           (float)
```

**Session-Skala (Ergänzung zu scaleRoot/scaleType, ClockState):**

- Skalen-Vollausbau: die globale Session-Skala unterstützt die 25 Scale-
  Presets in Ableton-Reihenfolge (Major … Pelog, 12-Bit-Maske pro Skala,
  Quelle: v1 TuringEngine — verifiziert gegen Live Scale Awareness 11.3+).
- `followAbleton`-Pattern: Skala kann via OSC von Live gesetzt werden
  (Root + 12-Bit-Maske als Atomics gestaged, Audio-Thread übernimmt am
  Blockanfang); manuelle Auswahl und OSC-Follow schließen sich pro
  Session aus.
- Scale-Quantisierung als Index-Mapping in die aktive Notenliste
  (jedes Bitmuster trifft eine gültige Note), nicht Nearest-Note-Rundung —
  klingt bei generativen Quellen (Turing/Random) deutlich musikalischer.

---

## 7. OSC-Integration

- OSC-Receive auf dediziertem Netzwerk-Thread (nicht Message Thread)
- Parameter-Updates via SPSC-Queue → Audio Thread (siehe 6.1 Dual-State)
- **Pfad-Schema:** `/conduit/{type}/{named_id}/value`
- Named IDs persistent über Ableton-Neustarts — kein Drag-and-Drop-Assignment

### 7.1 Auto-Registration via ValueTree::Listener

- `OscController` lauscht global als `ValueTree::Listener` auf Root-Tree
- `valueTreeChildAdded` → liest `type` + `moduleId` → registriert OSC-Adressen automatisch
- `OscController` cached `moduleId` bei `nodeState → Deleting` (Phase 1 Delete)
- OSC-Deregistrierung erfolgt in Phase 1, **nicht** erst in `valueTreeChildRemoved`
- DSP-Module wissen nichts von OSC — Single Responsibility

### 7.2 Link Audio (Audio in der Link-Session)

- **LinkAudio ERSETZT Link** — beide Klassen nie parallel
  instanziieren; `LinkClock`-Pimpl hält die einzige
  `ableton::LinkAudio`-Instanz für Timing UND Audio.
- IWYU: `<ableton/LinkAudio.hpp>` in JEDER Compilation Unit, die
  LinkAudio-Typen berührt.
- Format interleaved int16, Float→Int16 NUR mit TPDF-Dither
  (LCG); Sink-Größe in SAMPLES — Frames und Samples nie mischen.
- Send + Clock implementiert; **Receive offen** (Phase 2,
  beginBeats-Alignment). Spezifikation + Lektionen:
  **docs/LinkAudio.md — Pflichtlektüre vor jeder LinkAudio-Arbeit.**

### 7.3 OSC-Send (Parameter-Feedback an Clients)

- Snapshot-Diff via `juce::Timer` @ 30 Hz auf dem Message Thread,
  Cache-Key `(nodeUuid, paramId)`; der Audio Thread ist NIE
  beteiligt (3.1).
- Default-Port 9001, NICHT 9000 (Loopback-Schutz).
- Details (Echo-Suppression, Float-Diff-Falle, `/conduit/sync`,
  IP-Learn, `OscSendSettings`): **docs/OscSend.md**.

### 7.4 Max4Live-Announce (Remote-Module)

- `remoteId` = dokumentierte Ausnahme zur Laufzeit-ID-Regel (§6):
  in Live-Set UND Patch persistent, hartes Format `[A-Za-z0-9_-]`,
  max. 64 Zeichen.
- Details (Announce-Format, RemoteModuleBinder, Alias-Adressen,
  tintColour, Referenz-Device): **docs/M4LAnnounce.md**.

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

```cpp
struct CalibrationProfile {
    juce::String interfaceId;      // primärer Key: exakter Device-Name
    juce::String interfaceIdPrefix; // Fallback: Prefix ohne Suffix wie " (2)"
    float        dcOffset;
    float        gainTrim;
};

// Im HardwareIOModule::processBlock() — allocation-free:
float calibrated = (rawValue + profile.dcOffset) * profile.gainTrim;
```

**Profile-Matching bei USB-Reconnect (Reihenfolge):**
1. Exakter Name-Match (`"ES-3"` == `"ES-3"`)
2. Prefix-Match (ignoriert Suffix: `"ES-3 (2)"` → matched `"ES-3"`)
3. Kein Match → UI zeigt Kalibrierungs-Warnung, Profil auf Neutral (`dcOffset=0, gainTrim=1`)

Profile sind kanalspezifisch, persistent im ValueTree, user-adjustierbar.

### 8.2 CVTunerModule (AnalysisModule)

Natives Kalibrierungswerkzeug analog zu Ableton CV Tools — ohne M4L-Abhängigkeit.

**Ablauf:**
1. Gibt bekannten Referenz-CV-Wert aus (konfigurierbar: 0V, 1V, 2V, 5V) via ES-3/ESX-8CV
2. Misst Rückweg via ES-6 Eingang
3. Berechnet `dcOffset` und `gainTrim` aus Differenz
4. Schreibt `CalibrationProfile` in ValueTree → sofort aktiv
5. Wiederholbar pro Kanal

```cpp
class CVTunerModule : public AnalysisModule {
    // Schreibt NUR in ValueTree (CalibrationProfiles)
    // Niemals direkt in Audio-Pfad
    // Messung läuft auf separatem Analyse-Thread
};
```

### 8.3 Latenz-Trim für CV-Ausgänge

Hardware-Realität (v1-erprobt): Modulsysteme brauchen ms-genauen Versatz.
- Pro CV-Ausgangskanal: `shiftMs` (±50 ms), zusätzlich globales
  `globalShiftMs` — beide als Beat-Offset im Audio-Thread eingerechnet.
- Gehört ins CalibrationProfile bzw. den Kanal-State, user-adjustierbar.

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

- `juce_add_gui_app` mit `BUNDLE_ID` und `JUCE_USE_CORE_AUDIO=1`
- `AudioDeviceManager.setAudioDeviceSetup()` — sampleRate 48000, bufferSize 128
- Tatsächliche Buffer-Size nach Setup abfragen — Hardware kann Minimum erzwingen
- `initAudio()` reagiert defensiv auf abweichende Werte, kein Crash,
  Abweichung in ValueTree-Property `audioSetupWarning` speichern

### 9.2 Linux Kiosk-Mode (LinkBox)

- App startet fullscreen/borderless, kein Window Manager nötig
- Cursor ausblenden wenn Touch aktiv
- PREEMPT_RT: keine RT-inkompatiblen Kernel-Calls im Audio Thread
- Touchscreen-Kalibrierung beim Start prüfen (`xinput set-prop`)

---

## 10. UI & Input

### 10.0 Push-3-Design-System (Stand Juli 2026)

- **PushLookAndFeel** (`Source/UI/PushLookAndFeel`) ist Default-LookAndFeel
  der App (gesetzt im EngineEditor): Jost als App-Font (BinaryData,
  `Assets/Fonts/`, OFL), dunkle Kacheln (#262626 auf #121212/#1a1a1a),
  LED-Akzente (grün Play, rot Automate/Looper, cyan Link, orange Capture).
- **PushIcons** (`Source/UI/PushIcons`): ALLE Symbole als `juce::Path` aus
  einem normierten 0..1-Quadrat in die Ziel-Bounds skaliert — vektorbasiert,
  beliebig auflösungs-/DPI-fähig, keine Bitmaps. Bausteine: IconTile/
  TextTile/ValueTile (`PushTiles`).
- **TransportBar & Metronom:** TransportBar ersetzt die
  Modul-Button-Toolbar komplett; Transport-/Link-Zustand in
  `TransportSettings`; Metronom = allocation-free Click NACH dem
  GraphFader, Beat-Grenzen sample-genau. Spezifikation:
  **docs/Transport.md**.
- **Pages** (`Source/UI/PageHost`): Grid (Ω, Touch-Controller-Baukasten) · Mixer (∥∥)
  · Clip (▷▭, Fugue-Machine-Sequencer) · Device (|||, Patch-Canvas). Device
  und Grid (M1) sind implementiert — Mixer und Clip bleiben gestylte
  Platzhalter, je ein eigener Meilenstein (Roadmap 11).
- **Transport-/Link-Zustand** in `TransportSettings` (App-Zustand, Muster
  MeterSettings); der EngineProcessor speist LinkClock (Start/Stop-Sync,
  Clock-Offset ±100 ms als Beat-Lese-Versatz) und Metronom (Enable, Anker).
- **Looper (Retro-Looper + Vollausbau):** Engine-Level, kein Graph;
  MT→Audio via `SpscQueue<ClipCommand>`, Audio→MT via Retire-Queue —
  `free` NIE im Audio-Callback; Playhead sample-kontinuierlich, NIE
  roh aus `beatAtBlockStart` (Wall-Clock-Jitter); `prepareToPlay` →
  `clearAllClips()`. Spezifikation + Lektionen (inkl.
  CallbackTimingMonitor, Spektrum-View): **docs/Looper.md**.

- **Grid-Touch-Controller (Ω):** Kette Quelle → Voice-Modell →
  Sink; MPE-Zuteilung IN Conduit (`VoiceAllocator` + `MpeEncoder`);
  `GridVoiceEngine` ist Engine-Level, Message-Thread — kein
  Audio-Thread, kein Graph. Spezifikation + Meilensteinleiter:
  **docs/Grid.md**.

- Touch-first Design: `setAcceptsTouchEvents(true)`
- Minimale Touch-Target-Größe: 44px
- Vollständig Mouse/Keyboard-kompatibel — kein Touch-only Code
- **Schrift wird NIE horizontal gestaucht (User-Regel 07/2026):** bei
  Platzmangel die Schriftgröße reduzieren oder den Text kürzen — niemals
  quetschen. Konkret: `drawFittedText`/`drawLabel` immer mit
  minimumHorizontalScale = 1.0 (PushLookAndFeel::drawLabel erzwingt das
  app-weit), `Label::setMinimumHorizontalScale (< 1.0f)` ist verboten.
- Jedes UI-Element mit Touch-State reagiert in ≤ 1 Frame visuell
- Keine blockierenden Operationen im `paint()`-Callback
- Animationen via `juce::VBlankAttachment` (JUCE 7.0.3+)
- Scope-Ringbuffer: Audio-Thread schreibt, UI-Thread liest (lock-free), 30fps Refresh

### 10.1 Touch-Gesten

| Geste | Funktion | Priorität |
|---|---|---|
| 1 Finger Drag | Parameter-Sweep (CV-Wert) | P0 |
| 2 Finger Pinch | Range-Zoom Scope/Visualizer | P0 |
| Grid: 1 Finger (Sonne) | Note + Pitch-Bend (X) + Ausdruck (Y) | P0 |
| Grid: 2. Finger im Orbit (Mond) | Ring — Radius → Slide, keine neue Note | P0 |
| 2 Finger Rotate | LFO-Phase / Tuning | P1 |
| 3 Finger Tap | Snap-to-Zero / Reset | P1 |
| Long Press | Kontextmenü / Node-Eigenschaften | P2 |

---

## 11. Feature-Roadmap (Scope-Referenz)

| Feature | Version | Notiz |
|---|---|---|
| Gate, EQ, Compressor | v2.0 | ProcessorModule, ISidechain |
| CVTunerModule | v2.0 | AnalysisModule, CalibrationProfile |
| CLAP-Hosting | v2.x | PluginModule wraps AudioPluginInstance |
| IPolyphonic | v2.x | Interface vorbereitet, noch nicht implementiert |
| VST3-Hosting | v3.0+ | Steinberg-Lizenz, nach CLAP |
| Cardinal/VCV Integration | v3.0+ | Touch-native Modular UI |
| Link Audio Send (LinkAudioSendModule) | v2.0 | erledigt 07/2026 → docs/LinkAudio.md — §7.2 |
| Link Audio Receive | v2.x | NÄCHSTER MEILENSTEIN — Pflichtlektüre docs/LinkAudio.md; beginBeats()-Alignment, Monitoring-Latenz dokumentieren |
| OSC-Send (Snapshot-Diff, /conduit/sync, IP-Learn) | v2.0 | OscSendService, 7.3 |
| M4L-Announce (remoteId, Alias-Adressen, Tint) | v2.0 | RemoteModuleBinder, 7.4 |
| Max-Testdevice ConduitLFO | v2.0 | Tools/Max/ConduitLFO, kein Audio im Device |
| Expert-Sleepers-Encoder (ES-5/ES-4(0)/8CV/8GT) | v2.x | v1-Port vorhanden (EncoderEngines.hpp, MIT/VCV) — HardwareIOModule-Grundstein |
| Euclid-/Turing-Module | v2.x | v1-Engines als Referenz (Launch-Quant, parametrischer Swing, Scale-Quantize) |
| Push-3-Transport-Header (TransportBar, Metronom, globaler Swing) | v2.0 | erledigt 07/2026 → docs/Transport.md — §10.0 |
| FX-Chassis-Standard (I/O-Gains+Meter, CV/Parameter, Link-Send, Dev-Modus, Kurven, Control-Links, Defaults) | v2.0 | erledigt 07/2026 → docs/FxChassis.md — §4.6 |
| Looper-Page (Retro-Looper, Endlesss-Stil, MVP ein Loop) | v2.0 | erledigt 07/2026 → docs/Looper.md — §10.0 |
| Looper-Vollausbau (4 Looper × 4 Tracks × Slots, Clip-Grid, VARI/Reverse/×2÷2, Delete/Save-Gesten, OSC-Actions, Clip-Export) | v2.0 | erledigt 07/2026 → docs/Looper.md — §10.0 |
| Mixer-Page | v2.x | ∥∥-Icon, Channel-Strips (Capture-Buttons wandern dorthin) |
| Grid-Page (Touch-Controller-Baukasten) | Ω-Icon | M1 (MPE-Keyboard) erledigt 07/2026; Meilensteinleiter: docs/Grid.md |
| Clip-Page (Fugue-Machine-Sequencer) | v2.x | ▷▭-Icon, immer aktiv, CV- UND MIDI-Ziele |
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

```cmake
target_compile_definitions(${PROJECT_NAME} PUBLIC
    JUCE_MODAL_LOOPS_PERMITTED=0    # verhindert blockierende Modal-Loops im Message Thread
    JUCE_WEB_BROWSER=0              # keine unnötigen Abhängigkeiten
    JUCE_USE_CURL=0
)

# Plattform-conditional — NICHT global setzen:
if(APPLE)
    target_compile_definitions(${PROJECT_NAME} PUBLIC JUCE_USE_CORE_AUDIO=1)
elseif(WIN32)
    # ASIO erfordert Steinberg ASIO SDK (separater Download + Lizenz, nicht in JUCE!)
    # SDK-Pfad via JUCE_ASIO_SDK_PATH, erst dann:
    target_compile_definitions(${PROJECT_NAME} PUBLIC JUCE_ASIO=1)
endif()
```

### 13.3 Quick Start & Build-Workflow

```bash
# Configure (Windows — auf diesem System ist VS 2026 installiert, kein VS 2022)
cmake -B build -G "Visual Studio 18 2026" -A x64
# Configure (Ninja, alle Plattformen)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build --config Debug

# Test: Standalone-Target aus build/ ausführen

# Sanitizer-Presets (13.4):
cmake --preset asan && cmake --build --preset asan   # ASan (MSVC) — läuft lokal unter Windows
cmake --preset tsan && cmake --build --preset tsan   # TSan (Clang) — NUR Linux/macOS/WSL,
                                                     # unter Windows nicht verfügbar
# TSan + ASan laufen außerdem automatisch in GitHub Actions (Ubuntu) bei jedem
# Push auf master — .github/workflows/ci.yml ('tsan' + 'asan-linux' Presets)
```

### 13.4 Testing & Validierung

- **Framework:** Catch2 v3 via `FetchContent`, eigenes `ConduitTests`-Target
- **Pflicht-Unit-Tests vor Integration:** SPSC-Queues, ValueTree-Serialisierung/-Migration,
  Graph-Topologie-Änderungen (Fade-Zyklen, Batch-Coalescing), CalibrationProfile-Matching
- **ThreadSanitizer:** Eigene CMake-Preset/Config mit `-fsanitize=thread` (Clang) —
  Pflicht-Lauf für allen Code, der Thread-Grenzen überschreitet (SPSC, atomics, AsyncUpdater)
- **AddressSanitizer:** `-fsanitize=address` für Graph-Swap- und Delete-Pfade (Zombie-UI, Use-after-free)
- TSan/ASan-Builds laufen mit Dummy-Audio-Device — kein ASIO nötig

### 13.5 Projektstruktur

```
/
├── CMakeLists.txt
├── CLAUDE.md
├── Source/
│   ├── Core/            GraphManager, OscController, Datenmodell
│   ├── Modules/         ConduitModule + Subklassen (je Modul: .h/.cpp Paar)
│   ├── Interfaces/      IClockSource, IClockSlave, ISidechain, ...
│   ├── UI/              Components, nur ValueTree-gebunden
│   └── Util/            SPSC-Queue, Helpers
└── Tests/               Catch2-Tests, spiegelt Source/-Struktur
```

---

## 14. ADRs (Architecture Decision Records)

ADRs liegen in `docs/adr/` (append-only, wortgleich ausgelagert).
Index:
- 001 — SpscQueue als einziger Inter-Thread-Queue-Baustein + Repo-Rename
- 002 — Grid-Page als Touch-Controller-Baukasten
- 003 — Grid-Voice-Ausgabe: Quelle → Voice-Modell → austauschbare Sinks
- 004 — Subsystem-Dossiers unter docs/ (+ Descope 10-Finger-Panic)

---

*Conduit Alpha v3 — Claude Code Instructions v4.7  |  Juli 2026*
