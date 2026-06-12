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

**Step-Sequencer, Urzwerg-inspiriert:**
- Engine: 4×16 Steps, CV/Gate ×4, Scale-Quantize über globale Session-Skala (`scaleRoot`/`scaleType` im RootTree, reist pro Block im ClockState)
- UI: 4×16-Grid-Kachel, Scale-Auswahl in der Toolbar, Kontrollleiste für alle Engine-Parameter

## Nächste Kandidaten (offen, Reihenfolge nicht festgelegt)

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
