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
- Quell-Schlüssel: "master" | "hw:{paar}" | "out:{paar}" | "tap:{name}" —
  Auflösung zentral in `resolveLooperSourceKey` (EngineProcessor.cpp);
  Ausgangs-Paar-Taps (out{p}_l/_r) werden in prepareToPlay registriert.
