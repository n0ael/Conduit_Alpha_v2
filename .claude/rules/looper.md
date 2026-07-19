---
paths:
  - "Source/Core/Looper/**"
  - "Source/UI/Looper*"
  - "Tests/Core/Looper*"
  - "Tests/Core/BarSampleAnchorsTests.cpp"
  - "Tests/Core/CallbackTimingMonitorTests.cpp"
  - "docs/Looper.md"
---

# Rule: looper — Retro-Looper + Vollausbau (CLAUDE.md 10.0)

**Pflichtlektüre: docs/Looper.md** (Spezifikation + Lektionen inkl.
CallbackTimingMonitor, Spektrum-View, Snap-Declick, Duck, Lead-in).

- Engine-Level, KEIN Graph; MT→Audio via `SpscQueue<ClipCommand>`,
  Audio→MT via Retire-Queue — `free` NIE im Audio-Callback.
- Playhead sample-kontinuierlich, NIE roh aus `beatAtBlockStart`
  (Wall-Clock-Jitter-Lektion) — Beat-Messung jitter-frei aus der
  SampleClock.
- `prepareToPlay` → `clearAllClips()`.
- Launch-Quantisierung über das app-weite Enum
  (`Source/Core/LaunchQuantization.h`), Grid-Übertritte sample-genau
  (`LooperClipMath::gridCrossingOffset`, FP-Epsilon-Lektion).
- Quell-Schlüssel: "master" | "hw:{paar}" | "hwm:{kanal}" | "tap:{name}" —
  Auflösung zentral in `resolveLooperSourceKey` (EngineProcessor.cpp).
  Die Auflösung folgt der Capture-Registry SYNCHRON über
  `CaptureService::onRegistryChanged` → `applyLooperSourceArming` —
  Re-Materialisierung eines Looper-In-Moduls registriert seine Kanäle
  auf NEUEN Slots (gearmte alte binden Material als held); ohne den
  Hook läse der Looper dauerhaft stale Indizes (= Stille,
  Feldtest-Fund 19.07.2026).
  Mono-Quellen (hwm:, Mono-Taps) lassen right = −1 ⇒ 1-Kanal-Clip
  (Mono-Export ohne Suffix). "out:{paar}" ist Legacy (resolvet zu −1) —
  Ausgangs-Signale loopt man per Kabel ins Looper-In-Modul (ADR 010).
- Looper-I/O (ADR 010): `LooperBank::renderBlock` läuft VOR dem Graph
  (Playback liest nur committete Clips), `mixToOutput` NACH dem Graph;
  die Looper-Out-Module lesen `getAudioView()` im SELBEN Callback —
  View-Pointer NIE über den Callback hinaus halten. Anker −1 = „Kein
  Master-Out"; `sendMaster` pro Looper (LooperSettings) wirkt NUR auf
  den Master-Mix, nie auf die Pre-/Post-/Track-/Send-Busse.
- Big Looper Out (ADR 012): `looper_big_out` = Standard-Ausgangsmodul,
  Slots folgen AUTOMATISCH der Struktur (Track-Outs post-fader → Busse
  → Send 1–4 IMMER alle 4 → Master; alle stereo). trackBus ist zugleich
  das Render-Ziel (kein geteilter Scratch mehr). Kabel-Remap bei
  Struktur-Änderung NUR über Spec-Identität (syncLooperBigOutConfigs),
  Re-Mat EXPLIZIT anstoßen (gleiche Kanalzahl ⇒ kein Property-Trigger).
  Das alte looper_out heißt „Looper Out Mini" (factoryId unverändert).
- Delete-Gating (ADR 012): Looper-/Track-Delete direkt nur ohne Clips
  UND ohne Big-Out-Kabel; sonst X/OK-Dialog → Force-Delete in den
  `LooperTrashCan` (~180 s, ↺-Kachel): Clips DETACHEN statt deleteClip
  (Bank bleibt Besitzerin), Kabel spec-relativ sichern. prepareToPlay:
  `trash.clearWithoutDelete()` VOR `bank.prepare()`. Kein UndoManager
  für Clips/Struktur.
- Quellen-Combo listet NUR Looper-In-Slots (zuoberst) + Interface-
  Eingänge (Mono/Stereo folgt dem ∥-Pairing der ChannelNames).
- Slot-Namen/-Farben (19.07.2026): autoName = SIGNALKETTE „Quelle ·
  FX…" (resolveSourceChainLabel; refreshLooperInAutoNames bei jeder
  Kabel-Änderung; userName gewinnt, „ 2"-Suffix bei Kollision); Renames
  migrieren gespeicherte tap:-Keys (`CaptureService::onChannelRenamed`).
  Slot-/Waveform-/Clip-Farbe = geerbte Quellfarbe
  (`Core/SignalFlowColours`, geteilt mit dem NodeCanvas), Fallback
  nodeColour.
- looper_in-Default = 4× stereo + 4× mono (12 Kanäle) — exakt die
  CaptureService-Slot-Reserve. Looper starten ab Werk OHNE Quelle:
  dauerhaft gearmte Kanäle (alter „master"-Default!) blockieren die
  aufgeschobene Puffersatz-Erweiterung für immer.
