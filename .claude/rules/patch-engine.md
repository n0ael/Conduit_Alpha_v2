---
paths:
  - "Source/Core/GraphManager.*"
  - "Source/Core/GraphFader.*"
  - "Source/Core/EngineProcessor.*"
  - "Source/Core/NodeUiRegistry.*"
  - "Source/Modules/ConduitModule.*"
  - "Source/UI/NodeCanvas.*"
  - "Source/UI/NodeComponent.*"
  - "Source/**/*Preset*"
  - "Tests/Core/GraphManagerTests.cpp"
  - "Tests/Core/GraphFaderTests.cpp"
  - "docs/PatchEngine.md"
---

# Rule: patch-engine — Glitch-freier Graph-Swap (CLAUDE.md §5)

**Pflichtlektüre vor Arbeiten an GraphManager, Graph-Swap, Delete-Pfad
oder Preset-System: docs/PatchEngine.md** (4-Schritt-Swap, zweiphasiges
Delete, Preset-System, Batch-Coalescing, gefadete Re-Materialisierung
Looper-I/O — Nummern 5.1–5.7).

- Graph-Mutationen NUR auf Message Thread; alle patchbaren Aktionen
  (add, remove, connect, disconnect) durch den `UndoManager`.
- 4-Schritt-Swap (5.2): Async Prepare (manuelles `prepareToPlay()` VOR
  dem Swap; Fehler → `nodeError`-Property, Modul nicht aufnehmen — nie
  ignorieren) → DSP Fade-Out [Audio Thread] → Topologie-Swap [Message
  Thread via `AsyncUpdater`, **kein Busy-Poll, kein Timer** —
  self-re-dispatch bis `fadeComplete`] → DSP Fade-In.
- Zweiphasiges Delete (5.3): Phase 1 `nodeState → Deleting`, UI
  entkoppelt sich via Listener, `OscController` cached `moduleId` JETZT
  und deregistriert sofort. Phase 2 [nächster Frame via
  `VBlankAttachment`, erst wenn kein UI-Listener mehr auf dem Subtree]:
  `removeNode()`, Subtree-Entfernung (UndoManager), Destruktion.
  `nodeState` enum: Active | FadingOut | FadingIn | Deleting.
- Batch-Coalescing (5.5): `GraphManager` (erbt `AsyncUpdater`) führt
  EINEN gemeinsamen Fade-Zyklus/Graph-Swap pro Frame-Delta aus — nie
  einen pro Änderung. Gilt für Undo, Redo, Preset-Load, Bulk-Delete,
  Copy-Paste.
- Preset-Save (5.4) nur wenn `isDirty` nicht gesetzt (CLAUDE.md 6.1);
  Laden rebuildet den Graph aus dem Tree, CalibrationProfiles sofort
  aktiv.
- `SmoothedValue`-Rampzeit 5 ms default, pro Node konfigurierbar;
  `fadeComplete` ist `std::atomic<bool>` — kein Mutex; kein
  `new`/`malloc` während des gesamten Ablaufs.

## Lebensdauer-Kontrakte (Querschnitt, CLAUDE.md §5)

- **UI-Component hält niemals einen Pointer auf den Processor — nur auf
  den ValueTree-Subtree** (Zombie-UI-Schutz, Delete-Pfad 5.3); der einzige
  sanktionierte Laufzeit-Zugriff auf Modul-Objekte läuft über die
  NodeUiRegistry.
- Stille Lebensdauer-Kontrakte: Service-Pointer in UI (LevelMeter, Taps,
  ChannelNames, UiSettings …) sind EngineProcessor-Member und überleben
  jede UI; GraphManager-Service-Pointer folgen der Deklarationsreihenfolge
  im EngineProcessor.
