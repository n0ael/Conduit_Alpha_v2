# Conduit Alpha v2 — Claude Code Anweisungen
> C++20 + JUCE 8+  |  Modulares Audio/CV-Instrument  |  Stand: April 2026

---

## 1. Rolle & Kontext

Du bist ein C++20 und JUCE (v8+) Audio-Experte. Wir entwickeln "Conduit alpha v2", ein modulares Audio/CV-Instrument mit freiem Node-basiertem Patching — vergleichbar mit einem Hardware-Modular-Synthesizer.

Denke in Architektur und Modulen, bevor du Code schreibst. Liefere Code-Snippets immer sauber getrennt in Header (.h) und Source (.cpp). Bleib technisch, präzise und direkt. Keine Erklärungen, die nicht angefordert wurden.

---

## 2. Tech-Stack

- Streng C++20, JUCE Framework (NUR native Components, **KEIN ImGui!**), CMake, Ableton Link SDK
- CMake: Nutze `juce_add_plugin` / `juce_add_gui_app`, kein manuelles Linken
- Keine Raw Pointer — JUCE-SmartPointer oder `std::unique_ptr`
- `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` nicht vergessen
- **Kein `AudioProcessorValueTreeState` (APVTS)** — nicht geeignet für dynamische Node-Graphs

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
// - juce::AbstractFifo oder std::atomic<> für Parameter-Updates
// - SPSC-Ringbuffer zwischen UI-Thread und Audio-Thread
```

### 3.2 Latenz-Ziele

| Parameter | Zielwert | Fallback |
|---|---|---|
| Buffer Size | 32 Samples | 64 Samples |
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
  └── Nodes[]
       ├── nodeId         (juce::Uuid)
       ├── type           (ModuleType enum als String)
       ├── moduleId       (named_id, z.B. "neutron_filter")
       ├── stateVersion   (int, für Migration)
       ├── nodeState      (Active | FadingOut | FadingIn | Deleting)
       ├── nodeError      (String, leer wenn kein Fehler)
       ├── position       (x, y für UI)
       └── Parameters[]
            ├── id, value, min, max, default
  └── Connections[]
       ├── sourceNodeId, sourceChannel
       └── destNodeId,   destChannel
  └── CalibrationProfiles[]
       ├── interfaceId        (Hardware-Device-Name, primärer Key)
       ├── interfaceIdPrefix  (Prefix ohne Suffix wie " (2)", Fallback-Key)
       ├── dcOffset           (float)
       └── gainTrim           (float)
```

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

---

## 8. CV-Hardware-Kalibrierung

DC-coupled Interfaces (ES-3, ESX-8CV etc.) haben hardware-spezifische DC-Offsets und
Gain-Abweichungen. `0.0f` digital ≠ `0.000V` analog → Out-of-Tune bei 1V/Oct.

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
- `AudioDeviceManager.setAudioDeviceSetup()` — sampleRate 48000, bufferSize 32
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

- Touch-first Design: `setAcceptsTouchEvents(true)`
- Minimale Touch-Target-Größe: 44px
- Vollständig Mouse/Keyboard-kompatibel — kein Touch-only Code
- Jedes UI-Element mit Touch-State reagiert in ≤ 1 Frame visuell
- Keine blockierenden Operationen im `paint()`-Callback
- Animationen via `juce::VBlankAttachment` (JUCE 7.0.3+)
- Scope-Ringbuffer: Audio-Thread schreibt, UI-Thread liest (lock-free), 30fps Refresh

### 10.1 Touch-Gesten

| Geste | Funktion | Priorität |
|---|---|---|
| 1 Finger Drag | Parameter-Sweep (CV-Wert) | P0 |
| 2 Finger Pinch | Range-Zoom Scope/Visualizer | P0 |
| 10-Finger-Chord | Panic / All-Notes-Off | P0 |
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

*Conduit Alpha v2 — Claude Code Instructions v4.1  |  Juni 2026*
