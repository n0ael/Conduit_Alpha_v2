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

**Capture & Record — Meilenstein komplett (Bausteine 1–7).** Audio-Pendant zu "Capture MIDI": permanenter Pre-Roll, Gate-Detektion mit Auto-Kalibrierung, bedarfsgesteuerte RAM-Ringe, samplegenau alignter BWF-Export bei laufender Aufnahme, Toolbar/Panel-UI. Abschluss-Baustein 7 — Härtung (RT-Audit + Stress-Suite):

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

- Mixer-Modul (mehrere Inputs) — Capture-Kanal-Buttons wandern dann vom CapturePanel in die Channel-Strips, `stripName` ersetzt `in{N}` im Export-Dateinamen
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
