# Datenmodell (ValueTree-Schema & OSC Dual-State) — Subsystem-Dossier
> Ausgelagert aus CLAUDE.md v4.7 §6.1/§6.2, Juli 2026. Für Arbeiten an
> diesem Subsystem verbindlich wie die CLAUDE.md selbst (§1.1).
> Pflichtlektüre vor Schema-Änderungen, Migrationen und Arbeiten am
> OSC-Dual-State. Die Kern-Invarianten (Single Source of Truth,
> Message-Thread-only, Laufzeit-ID-Regel) stehen in CLAUDE.md §6.

## 6.1 OSC Dual-State (Echtzeit vs. UI-Konsistenz)

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
// OscController [Network Thread] — Ist-Implementierung (AsyncUpdater,
// kein rohes callAsync-Lambda):
void onParameterMessage (nodeUuid, parameterId, clamped) {
    audioQueue.push ({ target, clamped });          // 1. sofort in den Audio Thread

    {   // 2. gesammelt in den ValueTree (Message Thread drained)
        const juce::ScopedLock lock (treeUpdateLock);
        pendingTreeUpdates.push_back ({ nodeUuid, parameterId, clamped });
    }
    stateDirty.store (true, std::memory_order_release);  // VOR triggerAsyncUpdate
    triggerAsyncUpdate();
}

// [Message Thread]
void handleAsyncUpdate() {
    // Reset VOR dem Drain: eine Message, die währenddessen eintrifft,
    // setzt stateDirty danach wieder — schlimmstenfalls ein
    // überflüssiger Flush, nie ein verlorener Wert.
    stateDirty.store (false, std::memory_order_release);
    applyTreeUpdates();   // pendingTreeUpdates → rootTree (swap unter Lock)
}
```

**Serialisierungs-Regel (Ist-Kontrakt):** `isDirty`/`stateDirty` guarded
Preset-Save/getStateInformation über den synchronen `flushPendingUpdates()`.
Undo-Transaktionen flushen NICHT; OSC-Werte laufen undo-frei und geraten
nie in Undo-Deltas. Subtree-erfassende Transaktionen (z. B. Modul-Delete)
können daher einen ≤ 1 Frame alten Stand snapshotten — akzeptierte
Semantik. Dadurch gehen keine OSC-Werte beim Speichern verloren.

| Pfad | Zweck | Latenz-Anforderung |
|---|---|---|
| SPSC-Queue | DSP-Parameter | < 1ms |
| ValueTree async | UI + Serialisierung | 10–50ms akzeptabel |
| isDirty flag | Serialisierungs-Guard | `std::atomic<bool>` |

**Macro-Modulation (MIDI-Rig M5c, dokumentierte 6.1-Erweiterung):**
Der Tree trägt den BASISWERT eines Parameters, das
`getParameterTarget()`-Atomic den macro-EFFEKTIVEN Wert
(`GraphManager`-ParamModulationBus, `eff = clamp(base +
offset·userRange)`, Message Thread) — Präzedenz ist der OSC-Fastpath,
der das Atomic ebenfalls am Tree vorbei schreibt. Presets, OSC-Send
und Undo sehen nur die Basis; `syncParameterValue` komponiert den
aktiven Offset bei jedem Tree→Atomic-Spiegeln (auch beim
Rebuild-Re-Apply über `addNewNodes`). Details: docs/MidiRig.md M5c.

## 6.2 ValueTree Schema

```
RootTree
  ├── scaleRoot / scaleType   (globale Session-Skala: 0–11 + chromatic/major/minor/pentatonic;
  │                            reist pro Block im ClockState zu den Modulen)
  ├── globalSwing             (globaler Session-Swing 0..0.75, Header-Regler; reist im
  │                            ClockState — IClockSlaves mit lokalem Swing 0 folgen ihm,
  │                            lokaler Swing > 0 überschreibt pro Modul, CLAUDE.md 4.5)
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
       ├── remoteId       (optional: Announce-Bindung CLAUDE.md 7.4 — persistente
       │                   Gegenstelle im Live-Set, dokumentierte Ausnahme
       │                   zur Laufzeit-ID-Regel CLAUDE.md §6)
       ├── tintColour     (optional: Track-Farbe 0x00RRGGBB, folgt Re-Announce)
       ├── linkSendEnabled (bool: FX-Chassis-Send-Tap am Ausgang, CLAUDE.md 4.6)
       └── Parameters[]
            ├── id, value, min, max, default
            ├── role       ("dsp"|"chassis"|"cvAmount" — FX-Chassis CLAUDE.md 4.6)
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

**Laufzeit-IDs (Ergänzung zur Kern-Invariante CLAUDE.md §6):**

- **Session-transiente IDs nie serialisieren:** Connections/Referenzen
  auf Objekte, deren IDs pro Session neu vergeben werden, dürfen nicht
  in Presets landen (v1-Lektion: gespeicherte Encoder-Verbindungen luden
  als Phantom-Connections). In v2 sind Node-Uuids persistent — die Regel
  gilt für alles Künftige mit Laufzeit-IDs (z. B. Link-Audio-ChannelIds
  von Peers: discoverbar, nie Teil des Patches).

**Audio-I/O als reguläre Browser-Module (ADR 009 — umgesetzt
18.07.2026):**

- Audio-I/O sind reguläre Browser-Module (`AudioEndpointModule`,
  factoryIds `audio_input`/`audio_output`): voller Delete-Pfad,
  Mehrfach-Instanzen (Graph summiert nativ); der GraphManager zieht
  implizite Anker-Kabel zu den AudioGraphIOProcessor-Ankern. Default-Patch
  enthält Stereo-I/O (Migration rootStateVersion 3, kein Auto-Repair —
  Patch ohne Output = bewusste Stille).

**Session-Skala (Ergänzung zu scaleRoot/scaleType, ClockState):**

- Skalen-Vollausbau: die globale Session-Skala unterstützt die 25 Scale-
  Presets in Ableton-Reihenfolge (Major … Pelog, 12-Bit-Maske pro Skala,
  Quelle: v1 TuringEngine — verifiziert gegen Live Scale Awareness 11.3+).
  **Umgesetzt Block I (07/2026):** `scale::kScaleInfos` in
  Util/ScaleQuantizer.h (chromatic + 25, stabile String-IDs; Legacy
  "pentatonic" → majorPentatonic; dabei Minor-Masken-Bugfix Tritonus→Quinte).
- `followAbleton`-Pattern: Skala kann via OSC von Live gesetzt werden
  (Root + 12-Bit-Maske als Atomics gestaged, Audio-Thread übernimmt am
  Blockanfang); manuelle Auswahl und OSC-Follow schließen sich pro
  Session aus.
- Scale-Quantisierung als Index-Mapping in die aktive Notenliste
  (jedes Bitmuster trifft eine gültige Note), nicht Nearest-Note-Rundung —
  klingt bei generativen Quellen (Turing/Random) deutlich musikalischer.

## Modul-Hierarchie & Pflicht-API (ausgelagert aus CLAUDE.md v5.7 §4.1/§4.4)

Basisklassen (jedes Modul ist ein eigenständiger `AudioProcessor` im
`juce::AudioProcessorGraph`, CLAUDE.md §5):

```
ConduitModule                    (abstrakte Basis, erbt AudioProcessor)
├── GeneratorModule              LFO, Envelope, MIDI→CV
├── ProcessorModule
│    └── PluginModule            CLAP-Host wrapper (v2.x)
├── AudioEndpointModule          audio_input/audio_output (ADR 009 — reguläre Browser-Module)
├── IOModule                     Looper patch IN/OUT (ADR 010/013);
│                                HardwareIOModule (ES-3/ES-5/ESX-8GT/ESX-8CV)
│                                und NetworkIOModule (OSC ↔ Ableton M4L): Roadmap
├── AnalysisModule               Scope, Tuner, FFT, CVTunerModule
└── UtilityModule                Mixer, Attenuator, DC Block, Math, Offset
```

Pflicht-Methoden jedes Moduls:

- `createState()` — lazy, VOR `addNode()` aufgerufen, **nicht** im Konstruktor
- `getModuleId()` — named_id für OSC-Pfad, z.B. `neutron_filter`
- `getModuleDisplayName()` — lokalisierter UI-Name, getrennt von moduleId
- `getType()` — ModuleType enum für GraphManager
- `getStateVersion()` — int für Serialisierungs-Versioning (Rückwärtskompatibilität)

Mixin-Interfaces (IClockSource/IClockSlave/ISidechain/IStochastic/
IPolyphonic/ITouchMacro): Thread-Ownership je Interface ist in der
Header-Doku unter `Source/Interfaces/` normativ; Thread-Grenzen-
überschreitende Methoden nur via SPSC-Queue oder `std::atomic`
(CLAUDE.md §4.2). Beispiel-Komposition (Roadmap):
`class TuringModule : public GeneratorModule, public IClockSlave,
public IStochastic {};` — primärer CV/Trigger-Output via Basisklasse,
Takt + Zufall als Mixins [Audio Thread].
