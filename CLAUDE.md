# Conduit Alpha v3 — Claude Code Anweisungen
> C++20 + JUCE 8+  |  Modulares Audio/CV-Instrument  |  Stand: Juli 2026
> Repo: github.com/n0ael/Conduit (vormals Conduit_Alpha_v2 — Alt-Referenzen meinen dieses Projekt).

---

## 1. Rolle & Kontext

Du bist ein C++20 und JUCE (v8+) Audio-Experte. Wir entwickeln "Conduit alpha v3", ein modulares Audio/CV-Instrument mit freiem Node-basiertem Patching — vergleichbar mit einem Hardware-Modular-Synthesizer. Denke in Architektur und Modulen, bevor du Code schreibst. Liefere Code sauber getrennt in Header (.h) und Source (.cpp). Technisch, präzise, direkt; keine unaufgeforderten Erklärungen.

### 1.1 Subsystem-Dossiers & Rules (Pflichtlektüre)

- Dossiers `docs/{Subsystem}.md` + ADRs `docs/adr/` sind für Arbeiten am jeweiligen Subsystem verbindlich wie diese Datei; vor JEDER Änderung das Dossier vollständig lesen (Phase 1 des Auftrags listet die gelesenen Dossiers). Abgeschlossene Feature-Specs wandern ins Dossier — in der CLAUDE.md bleiben Invarianten + Verweis.
- Subsystem-INVARIANTEN leben als path-scoped Rules `.claude/rules/*.md` (ADR 005): sie laden mechanisch beim Read von Subsystem-Dateien — `paths:`-Frontmatter verwenden, NIE `globs:` (lädt fälschlich unconditional).
- Neue Dateien eines Subsystems triggern die Rule erst nach dem ersten Read — die Phase-1-Inventur (Bestandsdateien lesen) deckt das ab.
- Querschnitts-Regeln (v. a. §3 Audio Thread) bleiben bewusst unconditional hier.

---

## 2. Tech-Stack

- Streng C++20, JUCE Framework (NUR native Components, **KEIN ImGui!**), CMake, Ableton Link SDK.
- CMake: `juce_add_plugin` / `juce_add_gui_app`, kein manuelles Linken.
- Keine Raw Pointer — JUCE-SmartPointer oder `std::unique_ptr`; `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` nicht vergessen.
- **Kein `AudioProcessorValueTreeState` (APVTS)** — nicht geeignet für dynamische Node-Graphs.
- C++-Kompilierfallen (verschachtelte `Config`-Default-Ctor, `juce::Point` braucht `juce_graphics`): docs/Build.md.

---

## 3. Audio Thread

### 3.1 Lock-free & Allocation-free (Non-Negotiable)

VERBOTEN im Audio-Callback: Heap-Allokationen (new/malloc); Mutex-Locks (`std::mutex::lock`); UI-State-Reads ohne lock-free Queue; String-Operationen; OS-Calls (file I/O, logging); RT-inkompatible Kernel-Calls (PREEMPT_RT).

PFLICHT:
- Parameter-Updates via `SpscQueue` (Source/Util/SpscQueue.h) oder `std::atomic<>`; SPSC-Ringbuffer zwischen UI- und Audio-Thread.
- `juce::AbstractFifo` NICHT verwenden — SpscQueue ist der einzige Inter-Thread-Queue-Baustein (Catch2-getestet, TSan-abgedeckt).
- Zufall im Audio-Thread NUR via inline LCG (`state = 1664525*state + 1013904223`), nie rand()/<random>-Engines — heap-/lock-frei, deterministisch pro Seed (IStochastic).
- Zeitbasen: musikalische Zeit aus dem ClockState des Blocks, absolute Zeit in SAMPLES (SampleClock). Keine Wall-Clock-/ns-Mathematik im Audio-Thread mischen (v1-Lektion: Mischbetrieb erzeugt Drift-/Rundungsfehler an den Nahtstellen).

### 3.2 Latenz-Ziele

Buffer Size 32–128 Samples @ 48 000 Hz (Fallback 44 100 Hz); Warnung erst > 256, die Untergrenze beurteilt der XRun-Zähler live (keine starre Schwelle). Glass-to-Sound < 10 ms gesamt. Eigene DSP-Module: Low Latency hat Design-Priorität, Ausnahmen nur wo prinzipbedingt unkritisch (z. B. Reverb).

---

## 4. Modul-Hierarchie & Interfaces

ConduitModule ist die abstrakte Basis (erbt `AudioProcessor`); Kategorien (Generator/Processor/AudioEndpoint/IO/Analysis/Utility) + Pflicht-Methoden (`createState()` lazy VOR `addNode()`, `getModuleId`/`getModuleDisplayName`/`getType`/`getStateVersion`): **docs/DataModel.md** (Abschnitt „Modul-Hierarchie & Pflicht-API").

### 4.2 Mixin-Interfaces & Thread-Ownership

- Thread-Grenzen-überschreitende Interface-Methoden nutzen SPSC-Queue oder `std::atomic`; Interface-Methoden NIE vom falschen Thread aufrufen.
- Ownership je Interface: die Header-Doku in `Source/Interfaces/` ist normativ (welcher Thread welche Methode ruft).
- Sonderfall `IStochastic`: Seed-Updates laufen auf dem Message Thread.

### 4.5 Launch-Quantisierung (IClockSlave-Muster)

Grid-Enum zentral/app-weit (`Source/Core/LaunchQuantization.h`); kanonisches Muster (atomare Pending-Flags, sample-genaue Grid-Überquerung): Rule `transport`.

### 4.6 FX-Chassis-Standard — verbindlich für ALLE FX-Module

Jedes Audio-FX-Modul erbt `ProcessorModule` und implementiert NUR `prepareCore()`/`processCore()` — Chassis-Bestandteile NIE nachbauen. Invarianten: Rule `fx-chassis`; Spezifikation: **docs/FxChassis.md — Pflichtlektüre vor jeder FX-Arbeit.**

---

## 5. Patch-Engine (Glitch-freier Graph-Swap)

Invarianten: Rule `patch-engine`; Detail-Abläufe (4-Schritt-Swap, zweiphasiges Delete, Preset-System, Batch-Coalescing 5.1–5.7): **docs/PatchEngine.md — Pflichtlektüre vor Arbeiten an GraphManager, Graph-Swap, Delete-Pfad oder Preset-System.**

- `juce::AudioProcessorGraph` ist die DSP-Engine; jedes Modul ist ein eigenständiger `AudioProcessor` im Graph. Graph-Mutationen **NUR auf Message Thread**; alle patchbaren Aktionen durch den `UndoManager`.
- **UI-Component hält niemals einen Pointer auf den Processor — nur auf den ValueTree-Subtree** (Zombie-UI-Schutz, Delete-Pfad 5.3); sanktionierter Laufzeit-Zugriff auf Modul-Objekte ausschließlich über die NodeUiRegistry. Stille Lebensdauer-Kontrakte der Service-Pointer: Rule `patch-engine`.

---

## 6. Datenmodell & Threading

- `juce::ValueTree` ist die Single Source of Truth für **Zustand und Serialisierung**; der `AudioProcessor` besitzt den Root-Tree, der Editor ist read/listen-only.
- ValueTree-Mutationen **NUR auf dem Message Thread**; `juce::UndoManager` für alle patchbaren Mutationen. Kein APVTS.
- `createState()` erzeugt den Subtree (Properties/Defaults/Ranges); `GraphManager` hängt ihn via `addChild()` in den Root-Tree (nie das Modul selbst); jeder Subtree trägt `getStateVersion()` für Migration.
- **Session-transiente IDs nie serialisieren:** Referenzen auf Objekte mit pro-Session neu vergebenen IDs dürfen nicht in Presets landen. Node-Uuids sind persistent; die Regel gilt für alles Künftige mit Laufzeit-IDs (v1-Phantom-Lektion + Link-ChannelId-Detail: docs/DataModel.md).

### 6.1 OSC Dual-State (Echtzeit vs. UI-Konsistenz)

OSC-Changes laufen [Network Thread] parallel auf zwei Pfaden: SPSC-Queue → Audio Thread (sofort, lock-free, < 1 ms) UND `MessageManager::callAsync` → ValueTree (UI + Serialisierung, ~1 Frame). `isDirty` guarded Preset-Save/getStateInformation (synchroner `flushPendingUpdates`); Undo-Transaktionen flushen NICHT, OSC-Werte laufen undo-frei (Subtree-Transaktionen snapshotten daher ≤ 1 Frame alt — akzeptierte Semantik). Code-Muster + Latenz-Tabelle: docs/DataModel.md.

### 6.2 ValueTree Schema

Vollständiges Schema (RootTree, Nodes[], Parameters[], Connections[], CalibrationProfiles[], Session-Skala/`globalSwing`): **docs/DataModel.md — Pflichtlektüre vor Schema-Änderungen.**

- Jeder Node-Subtree trägt `stateVersion` (Migration) und `nodeState` (Active | FadingOut | FadingIn | Deleting).
- Audio-I/O sind reguläre Browser-Module (`AudioEndpointModule`, factoryIds `audio_input`/`audio_output`, ADR 009); Anker-Kabel, Stereo-Default + Migration-Details: docs/DataModel.md.

---

## 7. OSC-Integration

- OSC-Receive auf dediziertem Netzwerk-Thread (nicht Message Thread); Parameter-Updates via SPSC-Queue → Audio Thread (§6.1). Named IDs persistent über Ableton-Neustarts — kein Drag-and-Drop-Assignment.
- **Pfad-Schema:** `/conduit/{type}/{named_id}/value`.
- **OSC-Send:** Snapshot-Diff @ 30 Hz auf dem Message Thread, Default-Port **9001** (NICHT 9000 — Loopback-Schutz).
- Auto-Registration (7.1) + OSC-Send (7.3) + Max4Live-Announce/`remoteId` (7.4, dokumentierte Ausnahme zur Laufzeit-ID-Regel §6): Rule `osc-remote`; Details docs/OscSend.md, docs/M4LAnnounce.md.
- Link Audio (7.2, ERSETZT Link, int16+TPDF, WeakReference-Pflicht): Rule `linkaudio`, **docs/LinkAudio.md**.

---

## 8. CV-Hardware-Kalibrierung

- Interne Konvention: float ±1.0 == Full Scale des Interfaces; die Umrechnung in echte Volt passiert ausschließlich im HardwareIOModule über das CalibrationProfile (dcOffset/gainTrim + `fullScaleVolts`). UI zeigt Volt an, speichert normalisiert.
- Physik-Begründung + Spannungs-Beispiele: docs/Calibration.md. Invarianten (Profile-Matching, CVTuner schreibt NUR Profile, `shiftMs`/`globalShiftMs`): Rule `calibration`; Spezifikation 8.1–8.3: **docs/Calibration.md — Pflichtlektüre vor jeder Kalibrierungs-Arbeit.**

---

## 9. Plattformen & Backends

Primär: Windows/ASIO + macOS/CoreAudio (Dev/DAW). Sekundär: Linux Desktop (JACK/PipeWire), Linux Kiosk/LinkBox (PREEMPT_RT, Fullscreen), iOS (CoreAudio Remote I/O, touch-first). Vollständige Plattform-Matrix (inkl. iOS-Messwerte, FireWire-Option, macOS-/Linux-Setup): docs/Build.md.

**Plattform-Scope-Regel:** Kein plattformspezifischer DSP- oder UI-Code. Plattform-spezifisches Setup in `initAudio()`, im Fenster-/Input-Setup (Fullscreen, Edge-Gesten-Unterdrückung, Touch-Feedback — ADR 008) und in CMake ist explizit erlaubt.

---

## 10. UI & Input

Design-System (PushLookAndFeel, PushIcons/PushTiles, Pages) + UI-Regeln im Detail: Rule `ui-design` (lädt bei Arbeit unter `Source/UI/`). Querschnitts-Kern:

- Touch-first: minimale Touch-Target-Größe 44px, vollständig Mouse/Keyboard-kompatibel — kein Touch-only Code.
- Vier User-Regeln (Vollfassungen in Rule `ui-design`): **Gesten-Parität** (jede Geste in drei Pfaden Touch/Trackpad/Maus+Tastatur) · **Eingaberegeln seitenspezifisch** (jede Page ihre eigene Eingabe-Tabelle) · **Schrift wird NIE horizontal gestaucht** (minimumHorizontalScale = 1.0) · **UI-Framerate über `UiFramePacer`** (nativ mit Monitor-Rate, global gedeckelt — keine festen `startTimerHz`-Refreshes).
- UI-Components binden NUR an den ValueTree-Subtree, nie an den Processor (§5); Animationen via `VBlankAttachment`, kein Blocking in `paint()`.
- Seitenspezifische Eingabe-Tabellen: Grid → Rule `grid`, TouchLive/EQ8 → docs/TouchLive.md, Node-Patch-Editor → Rule `node-editor`/docs/NodeEditor.md; weitere Subsysteme: TransportBar/Metronom → Rule `transport`, Looper → Rule `looper`, MIDI-Rig → Rule `midirig`. App-weiter Gesten-Fallback (Sonne/Mond, Pinch, Rotate, Long Press): Rule `ui-design`.

---

## 11. Feature-Roadmap

docs/Roadmap.md (frei fortschreibbar, unabhängig vom Versionszyklus dieser Datei). Scope-Grenzen: §12.

---

## 12. Out-of-Scope (bewusst ausgeschlossen)

- ImGui-basierte Conduit v1-Architektur; M4L-Patchbay-Integration (Glymma-Scope); Hardware-Spezifikation LinkBox Mini/Pro; Rechtliche Struktur/UG-Gründung/Pricing; plattformspezifischer DSP-/UI-Code (Setup in `initAudio()` erlaubt); Android-Port (Markt faktisch iOS; zurückgestellt, Wiedervorlage nur bei belegter Nachfrage).
- Lizenz-/Vertriebs-/Pricing-Entscheidungen werden außerhalb des Repos geführt und sind hier bewusst nicht dokumentiert.

---

## 13. Tooling & Technische Guardrails

- **C++20 strikt** (`CMAKE_CXX_STANDARD 20` + `_REQUIRED ON`); JUCE ≥ 8.0.0 + Ableton Link via `FetchContent` (kein Submodule/System-Install).
- **Warnungen als Fehler:** `/W4 /WX` (MSVC), `-Wall -Wextra -Werror` (GCC/Clang) — per `set_source_files_properties` nur auf Conduit-eigenen Quellen (JUCE-Sources Werror-frei).
- RT-Safety-Defines: `JUCE_MODAL_LOOPS_PERMITTED=0`, `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`. Plattform-Defines NIE global: `JUCE_USE_CORE_AUDIO=1` nur APPLE; `JUCE_ASIO=1` nur WIN32 + Steinberg ASIO SDK (`JUCE_ASIO_SDK_PATH`).
- Configure (Windows, VS 2026, KEIN VS 2022): `cmake -B build -G "Visual Studio 18 2026" -A x64`, dann `cmake --build build --config Debug`.
- Sanitizer-Presets: `cmake --preset asan` (MSVC, lokal Windows) / `tsan` (Clang, NUR Linux/macOS/WSL); beide laufen zusätzlich in GitHub Actions (Ubuntu) bei jedem Push auf master.
- Airwindows-Targets: `ConduitAirwindows` (Static Lib) + `ConduitAirwindowsTests`. Details, Ninja-Variante, ASIO-SDK: docs/Build.md.

### 13.4 Testing & Validierung

- Catch2 v3 via `FetchContent`, eigenes `ConduitTests`-Target.
- Pflicht-Unit-Tests vor Integration: SPSC-Queues, ValueTree-Serialisierung/-Migration, Graph-Topologie (Fade-Zyklen, Batch-Coalescing), CalibrationProfile-Matching (fällig mit CV-Subsystem v2.0).
- **ThreadSanitizer** (`-fsanitize=thread`, Clang) Pflicht für allen Code über Thread-Grenzen (SPSC, atomics, AsyncUpdater); **AddressSanitizer** (`-fsanitize=address`) für Graph-Swap-/Delete-Pfade (Zombie-UI, Use-after-free).
- TSan/ASan-Builds laufen mit Dummy-Audio-Device — kein ASIO nötig.

### 13.5 Projektstruktur & Layering

- `Source/{Core,DSP,Interfaces,Modules,TouchLive,UI,Util}` + `Tests/` (spiegelt Source/). Je Modul ein .h/.cpp-Paar; Ausnahmen: Interfaces, Kategorie-Basisklassen, Header-only-Templates/POD (SpscQueue). `target_sources` registriert NUR Übersetzungseinheiten (.cpp).
- Soll-Layering (ADR 014): Util → nichts; Interfaces → Util; DSP → nichts Projektinternes; Core → Interfaces, Util; TouchLive → Core, Util; Modules → Core, Interfaces, Util, DSP; UI → Util, TouchLive, Core (ValueTree-/Settings-/Service-Header — KEINE Processor-/Modul-Header, §5; Laufzeit-Zugriff nur über NodeUiRegistry).
- Kompositions-Whitelist (dürfen zusätzlich Modules/UI/TouchLive inkludieren) + Begründung: ADR 014.

---

## 14. ADRs (Architecture Decision Records)

ADRs liegen in `docs/adr/` (append-only); vollständiger Index mit Titeln: **docs/adr/README.md** (001–017).

---

## Rollen & Gates

Leon (User): Produktvision, Design- und Verhaltensentscheidungen, Feldtests/Smoke am Instrument, Freigaben auf Berichtsebene. Leon liest KEINEN Code — Berichte, Smoke-Anleitungen und Entscheidungsvorlagen sind in Produktsprache zu schreiben; Diff-Review ist NICHT sein Gate.
Claude (Chat): Architektur, Entscheidungsvorlagen mit Tradeoffs, Auftrags-Autorschaft, Review von STOPP-Berichten/Fix-Vorschlägen.
Claude Code: Implementierung, Tests, Dossier-/STATUS-Pflege; committet selbstständig EINEN Commit pro Meilenstein.
Mechanische Gates: /WX-Build, Catch2 (Regressionen vorher-rot), ASan/TSan, RT-Audit, CI.
Push-Gate (Leon, Checkliste statt Diff): Bericht gelesen · DoD vollständig · eigener Smoke bestanden · CI grün → Push freigeben. Jeder Bericht endet mit 'Dein Smoke' in Produktsprache.
Externe/globale Skills und Tools überstimmen dieses Regelwerk nicht; bei Konflikt gilt die CLAUDE.md (Präzedenz: ponytail-Ausschluss).

---

*Conduit Alpha v3 — Claude Code Instructions v5.8  |  Juli 2026*
