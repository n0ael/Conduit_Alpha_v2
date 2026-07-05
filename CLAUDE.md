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

Jedes Audio-FX-Modul erbt von `ProcessorModule` und bekommt damit AUTOMATISCH
das Standard-Chassis. Neue FX-Module implementieren NUR `prepareCore()`/
`processCore()` (reine Stereo-Audio-Sicht, Kanäle 0..1) und liefern ihre
DSP-Parameter als `ChassisParamDesc`-Liste an den ProcessorModule-Konstruktor.
NIE `prepareToPlay`/`processBlock`/`appendParametersTo`/`getParameterTarget`
selbst überschreiben (final).

**Das Chassis stellt bereit (nicht optional, nicht nachbauen):**

- **Input-/Output-Gain** (`input_gain`/`output_gain`, −60..+6 dB, −60 = exakt
  Stille, role="chassis"), SmoothedValue 5 ms. Signal-Reihenfolge:
  noteBlockBegin → CV lesen → In-Gain → In-Meter → processCore → Out-Gain →
  Out-Meter → Link-Tap.
- **2×2-Kanal-LevelMeter** (eigene Instanzen im Modul; UI liest transient pro
  Tick über `GraphManager::getModuleFor` — nie Meter-Pointer cachen, 5.3).
- **Link-Audio-Send-Tap** (LinkSendTaps, Kanal-Name = moduleId, Node-Property
  `linkSendEnabled` = Patch-Zustand, undo-fähig via
  `GraphManager::setLinkSendEnabled`); Chassis implementiert ILinkAudioClient
  + IClockSlave — Injektion/Rename/Phase-1-Retire übernimmt der GraphManager.
- **Pro DSP-Parameter ein CV-Eingang**: Kanal-Layout FEST Audio 0..1, CV 2..N
  (CV-Kanal von Parameter i = 2+i, `ChassisSchema::cvChannelForParam`).
  **CV-Richtungs-Modell** (User-Entscheidung 07/2026): CV blockkonstant als
  Blockmittel des BETRAGS (Gleichrichtung VOR der Mittelung — bipolare
  Quellen werden zur Hüllkurve), die Richtung bestimmt allein der
  Attenuverter `{param}_cv_amt` (−1..+1, role="cvAmount"): positiv = vom
  Fader-Wert nach oben, negativ = nach unten.
  `effective = clamp(base + |cv|·amt·(userMax−userMin), userMin, userMax)`.
- **Control-Linking** (modulintern): `linkSource`/`linkAmount`/`linkCurve`
  pro dsp-Parameter — das Ziel folgt der normalisierten Stufe-1-Quelle als
  interne Modulation (folgt auch OSC/CV der Quelle, Ziel-Fader bleibt
  stehen). DSP zweistufig: Stufe 1 = base+CV, Stufe 2 = Links auf Stufe-1-
  Basis → Zyklen (A↔B) sind harmlos. Optionale Link-Response
  (`ChassisSchema::LinkResponse`: Bezier-Form + Start-/Endwert — FALLENDE
  Responses drehen die Richtung direkt in der Kurve, z.B. Auto-Gain/
  Gain-Matching). APIs: `setParameterLink`/`setParameterLinkCurve`.

**Schema-Regeln:**

- Parameter-Property `role`: "dsp" | "chassis" | "cvAmount" — die UI layoutet
  danach; OSC-Adressen bleiben kanonisch /conduit/processor/{moduleId}/{paramId}.
- `userMin`/`userMax`/`uiHidden`/`curve`/`linkSource`/`linkAmount`/`linkCurve`
  sind Patch-Zustand pro dsp-Parameter (Dev-Modus): der Fader nutzt die
  User-Range + Bezier-Kurve (reines UI-Mapping via CurvedSlider — im Tree
  steht IMMER der echte Wert); CV/Links wirken IM User-Bereich.
- Range-Edits clampen den Wert in DERSELBEN Undo-Transaktion
  (`setParameterUserRange`); `uiHidden` trennt CV-Kabel des Parameters in
  derselben Transaktion (keine Phantom-Modulation) und ändert NIE das
  Bus-Layout/numInputChannels (sonst Graph-Rebuild pro Klick).
- Bezier-Kurven: Kontrollpunkte via `ChassisSchema::parseCurve` auf [0,1]
  geclamped → x(t) UND y(t) monoton, Mapping eindeutig invertierbar.
- Modul-Typ-Defaults (`ModuleUiDefaults`, App-Zustand, Muster MeterSettings):
  „als Standard" im Dev-Modus sichert die dsp-Overrides pro factoryId;
  Overlay greift NUR bei Neu-Anlage (`addModuleNode`) — Presets/Patches
  gewinnen immer.
- Chassis-Versionierung: stateVersion ≥ 2; `ChassisSchema::migrate` läuft
  idempotent in `GraphManager::normalizeNode` für alle Processor-Nodes
  (Gains/Attenuverter/role ergänzen, Kanäle 0/1 stabil — Kabel überleben).

**UI:** `FxModulePanel` ist die Pflicht-Oberfläche aller Processor-Nodes
(Auswahl über `type == "Processor"`, nicht factoryKey) — links In-Zug
(GainFaderMeter: Fader + dB-Skala + Stereo-Meter + Clip-Reset), Mitte pro
sichtbarem dsp-Parameter eine vertikale Fader-Spalte (Titel / CurvedSlider /
Attenuverter-Knob / CV-Port), rechts Out-Zug + LINK-Button + Status-LED.
Dev-Modus-Toggle sitzt im Node-Header (transient pro Kachel, KEIN Patch-/
App-Zustand); der ~-Button jeder Spalte öffnet den CurveEditor (CallOutBox):
Tabs Fader/Link, draggbare Range-Endpunkte, Min/Max-Felder, Link-Quelle +
Amount. CV-Port-Anker delegiert `NodeComponent::getPortCentre` an
`FxModulePanel::cvPortCentre` (Kanäle ≥ 2).

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

### 7.2 Link Audio (Senden/Empfangen von Audio in der Link-Session)

- **LinkAudio ERSETZT Link** — beide Klassen nie parallel instanziieren
  (Header-Doku Link 4.0). `LinkClock`-Pimpl hält die einzige
  `ableton::LinkAudio`-Instanz für Timing UND Audio.
- **IWYU-Falle:** `<ableton/LinkAudio.hpp>` in JEDER Compilation Unit
  inkludieren, die LinkAudio-Typen berührt — die Link-Header sind nicht
  selbsttragend.
- **Format:** interleaved 16-bit signed int. Float→Int16 IMMER mit
  TPDF-Dither (LCG-basiert, kein rand()). Sink-Größe in SAMPLES anlegen
  (`samplesPerBlock * numChannels`) — Frames und Samples nie mischen.
- **Sinks senden erst, wenn mindestens eine Source subscribt** —
  Idle-Sinks sind gratis. UI unterscheidet „announced" vs. „streaming".
- **Threading:** `enableLinkAudio()`, `channels()`, Callbacks → Message
  Thread; `ChannelsChangedCallback` und Source-Callbacks kommen auf einem
  Link-Thread und werden via `MessageManager::callAsync` gemarshallt.
  `BufferHandle::commit()` ist RT-safe und nutzt dieselbe
  SessionState/Beat/Quantum-Basis wie das lokale Rendering (aus dem
  ClockState des Blocks — kein zweites captureAudioSessionState im Modul).
- **Sink-Lifecycle = zweiphasiges Delete:** Sink-Reset in Phase 1
  (Pattern OscController), sonst Zombie-Kanäle bei den Peers.
- **Kanal-Name = moduleId**, Rename via `sink.setName()` live propagiert.
- **Empfangen (Phase 2):** Buffer-Alignment über `BufferHandle::Info::
  beginBeats(sessionState, quantum)` — nie naiv FIFO'en (v1-Drift-Lektion).

### 7.3 OSC-Send (Parameter-Feedback an Clients)

- **Snapshot-Diff statt Listener:** ein `paramValue`-Listener kann
  OSC-Empfang, UI, Undo und Preset-Load nicht unterscheiden. Der
  `OscSendService` läuft deshalb als `juce::Timer` @ 30 Hz auf dem Message
  Thread: Tree-Walk über Nodes[], Diff gegen den `lastSent`-Cache mit Key
  **`(nodeUuid, paramId)`** (nicht Adresse — rename-sicher). Deleting-Nodes
  werden wie in der Receive-Registry übersprungen, Cache-Einträge
  verschwundener Nodes gepruned. Der Audio Thread ist NIE beteiligt (3.1).
- **Echo-Suppression:** `OscController::applyTreeUpdates()` meldet jeden in
  den Tree übernommenen Empfangswert via `onRemoteValueApplied` →
  `OscSendService::noteRemoteValue()` impft den Cache VOR dem nächsten
  Tick — der eigene Empfang wird nie zurückgesendet. UI/Undo/Preset-Load-
  Writes diffen dagegen und gehen raus (gewollt).
- **Float-Diff-Falle:** `var` speichert double, der Cache float — beidseitig
  über `float` vergleichen (`juce::exactlyEqual`), sonst Dauersende-Schleife.
- **Transport:** ein `OSCBundle` pro Tick, Chunking bei >50 Messages
  (UDP-Paketgrenze); `IOscSink`-Seam für Tests, `juce::OSCSender` in der App.
- **`OscSendSettings`** (App-Zustand, Muster `MeterSettings`): Host / Port /
  Enabled in `Conduit/OscSend.settings`. **Default-Port 9001, NICHT 9000**
  (Loopback-Schutz gegen den eigenen Empfang), Enabled default aus.
  Aktivierung leert den Cache → erster Tick ist ein impliziter Voll-Sync.
- **`/conduit/sync`:** Client fordert den Voll-Dump an — Erkennung VOR dem
  Endpoint-Lookup [Netzwerk-Thread], Ausführung via atomic Flag +
  AsyncUpdater auf dem Message Thread (`sendFullDump()`).
- **IP-Learn (Learn-Probe):** `juce::OSCReceiver` verwirft die Absender-IP
  (`socket->read(...)` ohne senderAddress, `OSCInputStream` nicht
  wiederverwendbar) — deshalb `OscController::beginIpLearn()`: Receiver
  kurz trennen, eigener `DatagramSocket` bindet den Empfangsport
  (Bind-Retry gegen das Rebind-Fenster, v.a. Windows), `read()` liefert die
  IP des ersten UDP-Pakets, Receiver wird restauriert (auch bei
  Timeout/Cancel/Destruktor). Kein OSC-Parsing nötig.

### 7.4 Max4Live-Announce (Remote-Module)

- **Format:** `/conduit/announce s:remoteId s:factoryKey s:trackName
  i:trackColour` (Farbe 0x00RRGGBB aus der Live API; Float32 wird toleriert
  — Max/js garantiert die Int-Kodierung nicht).
- **remoteId — dokumentierte Ausnahme zur Regel 6:** die ID ist in BEIDEN
  Welten persistent (Live-Set speichert sie als Device-Parameter „Stored
  Only", der Conduit-Patch als Node-Property `remoteId`) — keine
  Laufzeit-ID. Hartes Format statt Sanitizing: `[A-Za-z0-9_-]`, max. 64
  (wird Teil von OSC-Adressen; eine umgeschriebene ID fände ihr Live-
  Gegenstück nie wieder).
- **Verarbeitung:** Netzwerk-Thread validiert und sammelt (`pendingAnnounces`,
  eigener Lock) + `triggerAsyncUpdate` → Message Thread: `onAnnounce` →
  `RemoteModuleBinder::handleAnnounce()` (find-or-create). Existiert →
  idempotent (nur `tintColour` nachziehen; Name/Position sind nach der
  Erst-Anlage User-Hoheit). Neu → `ModuleFactory::isRegistered`-Whitelist,
  `addModuleNode(factoryKey, pos, configure)` (configure setzt
  remoteId+Tint VOR dem Einhängen), dann `renameNode` auf den Track-Namen
  (Kollision → Auto-Name bleibt).
- **Alias-Adressen (receive-only):** `rebuildEndpoints()` registriert für
  remoteId-Nodes ZUSÄTZLICH `/conduit/remote/{remoteId}/{paramId}` auf
  denselben Endpoint — das Device adressiert nur über seine remoteId,
  User-Renames und Kollisions-Suffixe sind ihm egal. Der Send-Pfad bleibt
  kanonisch (`/conduit/{type}/{moduleId}/{paramId}`, Helper `OscAddress.h`).
- **Kein Auth** (LAN-Annahme wie der übrige Empfang) — Whitelist +
  Zeichen-Limits + Idempotenz decken Garbage ab. Node in Conduit gelöscht,
  Device lebt → der 30-s-Re-Announce legt neu an (gleiche remoteId).
- **UI:** `NodeComponent` zeigt `tintColour` als Streifen unter der
  Kopfzeile, folgt Re-Announces live. Referenz-Device:
  `Tools/Max/ConduitLFO/` (kein Audio im Device — der LFO läuft nativ).

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
- **TransportBar** ersetzt die Modul-Button-Toolbar komplett: Play (Link
  Start/Stop-Sync), Tape (oo — Retro-Looper-Page, LED = Page offen ODER
  Loop spielt), Capture ⛶
  (Shift-Klick = Kanal-Panel), Fixed Length/Automate (persistierte
  Looper-Toggles), Tap-Tempo als Monitor (M4L-„TAP and CHANGE"-Modell:
  endloses Tappen misst NUR, Set-Kachel committet zur Session; Tap halten
  = Reset; Tap ▾ = Menü mit Auto-Commit ab Tap n fürs MIDI/OSC-Mapping +
  Reset-Haltedauer) + Nudge ±, Metronom ○●, Tempo/Position/Swing-Kacheln,
  Link ▾ (Menü: Start/Stop-Sync, Clock-Offset, Metronom-Ausgang),
  Page-Icons, „+"-Browser (Module + Presets), Undo (Shift-Klick = Redo),
  Save, ⚙, Skala.
- **Pages** (`Source/UI/PageHost`): Grid (Ω, Touch-Controller-Baukasten) · Mixer (∥∥)
  · Clip (▷▭, Fugue-Machine-Sequencer) · Device (|||, Patch-Canvas). Nur
  Device ist implementiert — die anderen sind gestylte Platzhalter, je ein
  eigener Meilenstein (Roadmap 11).
- **Transport-/Link-Zustand** in `TransportSettings` (App-Zustand, Muster
  MeterSettings); der EngineProcessor speist LinkClock (Start/Stop-Sync,
  Clock-Offset ±100 ms als Beat-Lese-Versatz) und Metronom (Enable, Anker).
- **Metronom** (`Source/Core/Metronome`): allocation-free Click NACH dem
  GraphFader auf ein wählbares Stereo-Paar; Beat-Grenzen sample-genau
  (floor-Überquerung, Muster 4.5), Downbeat oktavhöher, Disable lässt den
  Tail ausklingen. Bewusst kein isPlaying-Gate (Conduit läuft frei).
- **Retro-Looper** (`Source/Core/Looper` + `Source/UI/LooperPage`, Stand
  07/2026 — Endlesss-Muster auf Capture-Audio-Basis, MVP = ein Loop):
  - Immer aufnehmend: Quelle = Capture-Kanal („master" = Master-Output-Tap
    master_l/_r nach dem GraphFader | „hw:{paar}" | „tap:{name}"), Arming
    (`CaptureService::setChannelArmed`) hält das Gate zwangsweise offen.
    Quelle + Ausgabe-Paar persistiert in TransportSettings
    (looperSource/looperAnchor).
  - `BarSampleAnchors` [Audio]: Taktgrenzen sample-genau als gepackte
    64-bit-Atomics (16 Bit bar-Tag + 48 Bit Sample-Position — Paar in EINEM
    Wort, sonst Slot-Reuse-Race); Grenze 0 wird nie überquert → Commit
    braucht bars+1 Grenzen.
  - Commit [MT] = letzte 8/4/2/1 KOMPLETTE Takte via Segment-Klick auf den
    gestauchten Waveform-Strip (Dichte verdoppelt sich an den
    Segment-Grenzen; beat-indizierte Min/Max-Bins, binsPerBeat 32); Kopie
    über das zählerbasierte Export-Halte-Protokoll, Wrap-Crossfade liest
    einen Lead-in VOR dem Loop-Start (5 ms equal-power).
  - Playback (`LooperEngine`, Engine-Level wie Metronom, bewusst ohne
    EngineProcessor-Abhängigkeit — späteres LooperModule hostet dieselbe
    Klasse): Phase beat-abgeleitet [B−L, B) → Start sofort phasenstarr,
    kein Drift; 2 Voices × 60 s (~46 MB @48 kHz), Re-Commit/Stop mit
    5-ms-Voice-Fades. Session- ≠ Aufnahme-Tempo ⇒ Varispeed (MVP-Grenze).
  - **Playhead-Lektion (3.1 bestätigt):** beatAtBlockStart ist
    Wall-Clock-basiert und jittert um den Callback-Scheduling-Versatz —
    NIE direkt als Lese-Basis nutzen (hörbare Körnung). LooperEngine führt
    einen sample-kontinuierlichen Playhead: Messung aus SampleClock +
    jüngstem Takt-Anker, Korrektur slew-limitiert (0.2 % Varispeed), Snap
    nur bei echten Beat-Sprüngen — und NIE hart (Feld-Lektion 04.07.2026:
    Link-Grid-Re-Syncs ließen die Messung pro Takt springen, jeder rohe
    Snap war ein Splice-Klick): Snap erst nach snapConfirmBlocks Blöcken,
    dann Duck-Declick (5-ms-Rampe auf 0 → Sprung unter Stille → zurück);
    snapCount als Diagnose in der Looper-Statuszeile („N Re-Syncs" —
    häuft er sich, wackelt Link-Achse oder Audio-Callback; Anzeige nur
    im Dev-Modus, UiSettings::devMode).
  - **Callback-Timing-Diagnose** (`Source/Core/CallbackTimingMonitor`):
    XRun-Zähler (Callback-Start-Gap > 2× Blockdauer = Deadline-Riss) +
    Load in ‰ des Block-Budgets als Durchschnitt UND Peak, gemessen um den
    GESAMTEN processBlock (QPC-Wall-Clock als dokumentierte 3.1-Ausnahme,
    NUR Diagnose, nie Zeitbasis). TransportBar zeigt „DSP x % ⌀ / y % pk ·
    N XRuns" rechts neben der Setup-Warnung — Durchschnitt = Ableton-CPU-
    Meter-Semantik, Peak = XRun-Frühwarner. EIGENER Settings-Schalter
    `UiSettings::dspMeter` (Default an), bewusst UNABHÄNGIG vom Dev-Modus
    (User-Entscheidung 04.07.2026). Peak-vs-Durchschnitt-Lektion: der
    Peak fängt den einen Block mit der Spektrum-FFT ein und liegt im
    Debug-Build ~10× über Release — CPU-Vergleiche NUR im Release-Build.
  - **Spektrum-View:** der Strip schaltet per Spectrum-Kachel (persistiert
    als looperSpectrum, TransportSettings) auf ein Spektrogramm um —
    zweiter always-on Tap-Pfad (FFT 2048/Hann, 64 Log-Bänder via
    looper::SpectrumBands, 16 Spalten/Beat), Rendering als
    ring-adressiertes Beat-Raum-Image + Segment-Blits (Fire-Palette);
    Segment-Klicks/Commit identisch in beiden Views.

- **Looper-Vollausbau (M1–M10, 07/2026 — ersetzt das Ein-Loop-MVP oben;
  dessen Lektionen [Playhead, Snap-Duck, Anker] gelten unverändert):**
  - **Struktur:** bis 4 Looper (eigene Quelle + eigener WaveformTap) ×
    bis 4 Tracks × 12 Clip-Slots (sichtbar 4–12 im Menü), Endless-Modell
    (immer-aufnehmend, Segment-Klick committet 8/4/2/1 Takte in den
    Target-Slot). Ein spielender Clip pro Track (Session-Verhalten).
  - **Engine:** `LooperBank` (ersetzt LooperEngine) — Voices REFERENZIEREN
    right-sized `LooperClip`s (RAM-Konto statt Prealloc, Default 1,5 GB);
    MT→Audio via SpscQueue<ClipCommand>, Audio→MT via Retire-Queue
    (Delete wandert IMMER durch den Audio-Thread, free nie im Callback,
    exportPins verzögern die Freigabe). Clip-Parameter als Staged/Active-
    Protokoll: der Audio-Thread wendet mit SEINEM Playhead positions-
    kontinuierlich an (LooperClipMath, geschlossene Re-Anker-Formeln);
    inhärente Lese-Sprünge deckt ein 5-ms-Splice-Duck pro Clip.
    Quantisierte Aktionen (Start phasenstarr / Retrigger Anker=Grid /
    Stop) als Pending-Action pro Track, sample-genau am Grid
    (`LaunchQuantization.h` = app-weites Enum, 4.5). Track-Mix: Gain-Slew,
    Balance-Pan (Mitte Unity), Mute/Solo (Scope pro Looper/global),
    Post-Fader-Meter. `LooperSessionModel` = MT-Slot-/Target-/Aktiv-
    Zustand über der Bank (EngineProcessor-frei, module-ready — einzige
    Lücke fürs LooperModule: BarSampleAnchors-Injektion). WICHTIG:
    prepareToPlay → `clearAllClips()` (Zombie-Pointer-Feld-Fund 05.07.).
  - **Persistenz:** `LooperSettings` (Conduit/Looper.settings, ValueTree↔
    XML): Struktur/Quellen/Mixer + alle Menü-Optionen (Quantisierung,
    Tap-Modus, ÷2-Hälfte, Reverse-Punkt, VARI-Raster Halbtöne|Session-
    Skala, VARI-Scope, Solo-Scope, sichtbare Slots, Delete-Latch,
    Auto-Advance). Clips selbst session-flüchtig (Save-Geste exportiert).
    Arming = VEREINIGUNG aller Looper-Quellen (Refcount-Diff — geteilte
    Quelle bleibt offen, bis der letzte Looper sie verlässt).
  - **UI:** Looper NEBENEINANDER (Mock-Layout); Fader OBEN mit vertikalem
    Wischen; VARI-Rotary in Oktaven (Detent 1×, DK-Reset, Rast-Button,
    Sync-Reset); TARGET-Kurzklick zykelt Tracks, Halten+Tap = Aktiv-
    Auswahl. Header-Kontext (nur Looper-Page): DELETE-/SAVE-HoldTiles
    (halten + Ziel antippen; Delete-Latch-Option) — Session-Save liegt
    im Browser (PROJEKTE → „Session speichern…").
  - **OSC-Actions** (`/conduit/looper/…`, Indizes 1-basiert): stop |
    {n}/commit i:bars | {n}/stop | {n}/track/{t}/stop | {n}/target
    i:track i:slot — Muster /conduit/sync (Erkennung vor Endpoint-Lookup,
    AsyncUpdater → onLooperAction), fire-and-forget.
  - **Clip-Export** (Save-Geste): `LooperClipExporter` → CaptureWriter-Job
    (_l/_r, eingefrorene TrackSource, startPosition = commitStartSample →
    bext-align zu Capture-Exports; `CaptureService::enqueueExternalJob`).

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
| Link Audio Send (LinkAudioSendModule) | v2.0 | NetworkIOModule, Sink = moduleId |
| Link Audio Receive | v2.x | beginBeats()-Alignment, Monitoring-Latenz dokumentieren |
| OSC-Send (Snapshot-Diff, /conduit/sync, IP-Learn) | v2.0 | OscSendService, 7.3 |
| M4L-Announce (remoteId, Alias-Adressen, Tint) | v2.0 | RemoteModuleBinder, 7.4 |
| Max-Testdevice ConduitLFO | v2.0 | Tools/Max/ConduitLFO, kein Audio im Device |
| Expert-Sleepers-Encoder (ES-5/ES-4(0)/8CV/8GT) | v2.x | v1-Port vorhanden (EncoderEngines.hpp, MIT/VCV) — HardwareIOModule-Grundstein |
| Euclid-/Turing-Module | v2.x | v1-Engines als Referenz (Launch-Quant, parametrischer Swing, Scale-Quantize) |
| Push-3-Transport-Header (TransportBar, Metronom, globaler Swing) | v2.0 | erledigt 07/2026 — 10.0 |
| FX-Chassis-Standard (I/O-Gains+Meter, CV/Parameter, Link-Send, Dev-Modus, Kurven, Control-Links, Defaults) | v2.0 | erledigt 07/2026 — 4.6 |
| Looper-Page (Retro-Looper, Endlesss-Stil, MVP ein Loop) | v2.0 | erledigt 07/2026 — 10.0 |
| Looper-Vollausbau (4 Looper × 4 Tracks × Slots, Clip-Grid, VARI/Reverse/×2÷2, Delete/Save-Gesten, OSC-Actions, Clip-Export) | v2.0 | erledigt 07/2026 — 10.0; LooperModule + MIDI-Input + Drag-to-DAW später |
| Mixer-Page | v2.x | ∥∥-Icon, Channel-Strips (Capture-Buttons wandern dorthin) |
| Grid-Page (Touch-Controller-Baukasten) | Ω-Icon | benutzerfreundlicher Baukasten für Touch-Controller-Layouts, die interne und externe Ziele steuern. AbletonOSC-Remote (Live fernsteuern) ist eines dieser Ziele, keine eigene Page. Meilenstein-Leiter:
  M1  Voice-Engine + direkter MIDI-Sink + spielbares 2-Stimmen-MPE-Keyboard (Circle-Mechanik, Release = Finger heben, Rand-Ribbons, Release-All)
  danach unabhängig, Reihenfolge nach Priorität:
    - OSC-Sink + Transcoder (Remote, cross-platform)
    - Gesten-State-Machine (Drone/Latch per Abhebe-Reihenfolge, Pinch-weg, Doppeltipp, Drift-über-Rand-und-Faden)
    - CV-Sink (Software-CVC)
    - Hardware-MPE-Input (macht Conduit zum Hub; mit CV-Sink = Haken CVC in Software)
    - Chord-Squares + Save/Load (Browser, Factory-Sets zum Losjammen ohne Theorie)
    - Omnichord-Strings |
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

### ADR: SpscQueue als einziger Inter-Thread-Queue-Baustein + Repo-Rename-Klarstellung

- **(a) AbstractFifo-Restbestand entfernt:** §3.1 nannte noch
  `juce::AbstractFifo` als Option für Parameter-Updates — ein Restbestand
  aus frühen Dokumentversionen. Die Codebase nutzt durchgängig `SpscQueue`
  (`Source/Util/SpscQueue.h`, Catch2-getestet, TSan-abgedeckt); es gibt
  0 AbstractFifo-Vorkommen im Code. §3.1 schreibt SpscQueue jetzt als
  einzigen Inter-Thread-Queue-Baustein vor.
- **(b) Repo-Umbenennung dokumentiert:** `Conduit_Alpha_v2` →
  `github.com/n0ael/Conduit` (Juli 2026). Der Hinweisblock unter dem
  Dokumenttitel verhindert Verwechslung alter Referenzen (Commits,
  Kommentare, externe Notizen) mit einem anderen Projekt; Roadmap-Angaben
  „v2.x" bleiben Feature-Meilensteine, keine Repo-Namen.
- Kein Code betroffen — reine Dokumentationsentscheidung.

### ADR: Grid-Page als Touch-Controller-Baukasten

Status: Akzeptiert — Juli 2026

Kontext:
Grid war als AbletonOSC-Remote-Page geführt (Conduit steuert Live fern).
Neue Produktabsicht: Grid wird der benutzerfreundlichste Touch-Controller-
Baukasten. Nutzer ohne Programmierkenntnisse bauen Controller-Layouts, die
interne (Conduit-Parameter/Makros) und externe Ziele steuern. Positionierung:
kein TouchOSC-Konkurrent, sondern mehr Gestaltungsfreiheit für Nicht-Programmierer.

Entscheidung:
1. Grid = Touch-Controller-Baukasten. AbletonOSC-Remote wird ein Ausgangs-Ziel
   innerhalb von Grid, keine eigene Page.
2. Architektur-Rückgrat, symmetrisch: mehrere Quellen -> ein internes
   Voice-/Control-Modell -> mehrere austauschbare Sinks, Quellen hinter einem
   Source-Interface und Sinks hinter einem Sink-Interface.
   - Quellen: Grid-Touch (u.a. MPE-Keyboard mit Circle-Mechanik),
     Hardware-MPE-Input (USB/DIN).
   - Sinks: (A) direkt MPE-MIDI (virtueller Port same-machine / Hardware-Port
     USB+DIN / macOS RTP-MIDI fuer Netz); (B) MPE-ueber-OSC (UDP -> externer
     Transcoder oder Max-Patch, remote/cross-platform); (C) CV (Software-CVC,
     per-Voice Pitch/Pressure/Slide auf DC-gekoppelte Outs, ES-9/MOTU).
3. Die MPE-Zuteilung (Finger -> Voice -> Kanal) liegt IN Conduit (testbar).
   Das OSC-Protokoll traegt bereits zugeteilte Voices, damit der externe
   Transcoder ohne musikalische Logik bleibt (Leos Max/JS-Domaene).
4. Same-machine nutzt den direkten MIDI-Sink; der OSC-Sink ist nur fuer Remote.
5. Circle-Mechanik: erster Finger = Note + Pitch-Bend ueber X + eine durchgehende
   Achse ueber Y; zweiter Finger spannt einen Kreis, dessen Radius eine zweite
   durchgehende Achse steuert. Y-Achse und Radius mappen auf die zwei
   durchgehenden MPE-Dimensionen (Channel Pressure / CC74-Slide), pro Element
   vertauschbar. Detail-Spezifikation im jeweiligen Meilenstein-Auftrag.

Konsequenzen:
+ Eine Engine, viele Ziele. Nicht-Programmierer erhalten Gestaltungsfreiheit.
+ Remote = OSC: netz-nativ, cross-platform, passt zur bestehenden OSC-Infra
  und zum Dedicated-Hardware-Ziel.
- MPE ist MIDI 1.0 (kein MIDI-2.0-Bedarf fuer dieses Feature).
- Direkter MIDI-Out-Sink ist neue Infrastruktur (§4.1 kennt bisher kein
  MIDI-Out-Modul). Virtueller Port ist plattformabhaengig.
- Die Circle-Gesten-Disambiguierung (Latch/Pinch/Drift) braucht eine eigene
  State-Machine und ist ein eigener Meilenstein.
- Windows-Kontext (Juli 2026): natives Network-MIDI noch Preview/Alpha ->
  der OSC-Weg ist die tragfaehige Remote-Loesung; nativer virtueller
  Port/Loopback ist inzwischen vorhanden.

---

*Conduit Alpha v3 — Claude Code Instructions v4.5  |  Juli 2026*
