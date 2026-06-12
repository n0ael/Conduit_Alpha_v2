# Conduit Alpha — Projektstatus

> Letzte Aktualisierung: 2026-06-12 | wird nach jedem Meilenstein gepflegt
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

## Aktueller Meilenstein (Juni 2026 — abgeschlossen)

**Capture-System, Baustein 4 — Gate-Detektion + AutoCalibrator (`Source/Core/Capture/`):**
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

- Capture-Baustein 5: Capture-Trigger/Export (WAV, Leser-Halte-Protokoll am `CaptureRingBuffer`); danach Settings-UI (async Resize-Confirm, RAM-Warnungs-Listener, Gate-Status pro Kanal via `getGate()`)

- Mixer-Modul (mehrere Inputs)
- Envelope-Modul (`IClockSlave`)
- CVTunerModule + Kalibrierungs-Workflow (CLAUDE.md 8)
- Touch-Gesten P0: Pinch-Zoom, 10-Finger-Panic (CLAUDE.md 10.1)

## Bewusst verschoben

- **ASIO:** wartet auf manuellen Steinberg-SDK-Download (CMake-Hook `JUCE_ASIO_SDK_PATH` existiert bereits)
- **MIDI 2.0:** bleibt Roadmap; MIDI→CV-Modul startet später mit MIDI 1.0
- **LinkBox-Prototyp:** alter i7-3770K-PC wird als physisches Linux-Testsystem aufgesetzt (JACK/PipeWire, Integrations-/Latenztests — nicht für Sanitizer)

## Arbeitsweise pro Meilenstein

Implementieren → Build + Catch2-Tests → ASan-Lauf → App-Smoke-Test mit Screenshot → Commit einzeln pro Meilenstein → CI beobachten.
