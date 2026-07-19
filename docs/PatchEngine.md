# Patch-Engine (Glitch-freier Graph-Swap) — Subsystem-Dossier
> Ausgelagert aus CLAUDE.md v4.7 §5, Juli 2026. Für Arbeiten an
> diesem Subsystem verbindlich wie die CLAUDE.md selbst (§1.1).
> Pflichtlektüre vor Arbeiten an GraphManager, Graph-Swap,
> Delete-Pfad oder Preset-System.

## 5.1 Architektur

- `juce::AudioProcessorGraph` ist die DSP-Engine
- Jedes Modul ist ein eigenständiger `AudioProcessor` im Graph
- `ValueTree` + `UndoManager` für Zustand, Serialisierung, Undo/Redo, UI-Binding
- Graph-Mutationen (`addNode` / `removeConnection`) **NUR auf Message Thread**
- Alle patchbaren Aktionen (add, remove, connect, disconnect) gehen durch `UndoManager`

## 5.2 Modul hinzufügen / Kabel umstecken (4-Schritt Ablauf)

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

## 5.3 Modul löschen (Zweiphasiges Delete — Zombie-UI-Schutz)

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

## 5.4 Preset-System (Speichern / Laden)

```cpp
// Speichern: nur wenn kein dirty-Flag gesetzt (siehe CLAUDE.md 6.1)
juce::ValueTree snapshot = rootTree.createCopy();
juce::XmlElement* xml = snapshot.createXml();
xml->writeToFile(presetFile, {});

// Laden:
juce::XmlElement xml = juce::XmlDocument::parse(presetFile);
juce::ValueTree loaded = juce::ValueTree::fromXml(*xml);
// → Graph-Manager rebuildet AudioProcessorGraph aus geladenem Tree
// → CalibrationProfiles werden mitgeladen und sofort angewendet
```

## 5.5 Batch-Coalescing (Undo / Bulk-Delete)

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

## 5.6 Regeln

- `SmoothedValue`-Rampzeit: 5ms default, konfigurierbar pro Node
- `fadeComplete` ist `std::atomic<bool>` — kein Mutex
- Kein `new`/`malloc` während des gesamten Ablaufs
- `prepareToPlay()`-Fehler → `nodeError`-Property, nie ignorieren
- Mehrere gleichzeitige Graph-Änderungen immer zu einem einzigen Swap coalescing

## 5.7 Gefadete Re-Materialisierung (Looper-I/O, ADR 010)

Slot-Umbauten an `looper_in`/`looper_out` (Kanalzahl-Properties bzw.
`outputPre`/`outputTarget`/`outputMode` an `<Output>`-Kindern) landen in
`pendingRematerialize` + `markTopologyDirty()`: der NÄCHSTE gefadete Swap
entfernt den alten Graph-Node (Capture-Kanäle werden EXPLIZIT per
Phase-1-Hook gelöst — die Node-Destruktion des AudioProcessorGraph läuft
deferred, der Destruktor käme für die Registry zu spät) und
`addNewNodes()` materialisiert frisch aus dem Tree. Eine bereits
vorbereitete Instanz (Schritt 1 eines laufenden Swaps) wird verworfen
(`preparedModules.erase`) — sie trüge die alte Slot-Konfiguration.
Anders als der harte Endpunkt-Pfad (Gerätewechsel, ADR 009) läuft dieser
Umbau am SPIELENDEN System und gehört deshalb hinter den Fade.
