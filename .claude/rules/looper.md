---
paths:
  - "Source/Core/Looper/**"
  - "Source/Core/LooperSettings.*"
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
  Re-Materialisierung eines Looper-patch-IN-Moduls registriert seine Kanäle
  auf NEUEN Slots (gearmte alte binden Material als held); ohne den
  Hook läse der Looper dauerhaft stale Indizes (= Stille,
  Feldtest-Fund 19.07.2026).
  Mono-Quellen (hwm:, Mono-Taps) lassen right = −1 ⇒ 1-Kanal-Clip
  (Mono-Export ohne Suffix). "out:{paar}" ist Legacy (resolvet zu −1) —
  Ausgangs-Signale loopt man per Kabel ins Looper-patch-IN-Modul (ADR 010).
- Looper-I/O (ADR 010): `LooperBank::renderBlock` läuft VOR dem Graph
  (Playback liest nur committete Clips), `mixToOutput` NACH dem Graph;
  die Looper-patch-OUT-Module lesen `getAudioView()` im SELBEN Callback —
  View-Pointer NIE über den Callback hinaus halten. Anker −1 = „Kein
  Master-Out"; `sendMaster` pro Looper (LooperSettings) wirkt NUR auf
  den Master-Mix, nie auf die Pre-/Post-/Track-/Send-Busse.
- Looper patch OUT (ADR 012 „Big Looper Out", umbenannt ADR 013):
  `looper_patch_out` (`LooperPatchOutModule`) = EINZIGES Ausgangsmodul,
  Slots folgen AUTOMATISCH der Struktur (Track-Outs post-fader → Busse
  → Send 1–4 IMMER alle 4 → Master; alle stereo). trackBus ist zugleich
  das Render-Ziel (kein geteilter Scratch mehr). Kabel-Remap bei
  Struktur-Änderung NUR über Spec-Identität (syncLooperPatchOutConfigs),
  Re-Mat EXPLIZIT anstoßen (gleiche Kanalzahl ⇒ kein Property-Trigger).
  Kachel (19.07.2026): Stereo-Meter pro Zeile aus
  `EngineProcessor::looperOutLevels` — Kanal-Layout STABIL im 4er-Raster
  (`meterChannelOf`, 50 Kanäle), NIE aus Spec-Reihenfolge rechnen;
  Track-Nummern GLOBAL im 4er-Raster (`globalTrackNumber`). Slot-Farben
  = eingefrorene Clip-Farbe (`LooperClip::sourceRgb`, Commit) bzw.
  blendRgb der summierten Clips (Busse/Sends/Master) über
  `NodeCanvas::onResolveLooperOutColour` (Editor-Resolver, 15-Hz-Hash).
  Das kompakte looper_out („Looper Out Mini") ist ENTFERNT (ADR 013);
  Alt-Schlüssel looper_in/looper_big_out migrieren beim Laden
  (`GraphManager::normalizeLoadedNodes`), looper_out-Nodes laufen in
  den nodeError-Pfad.
- Delete-Gating (ADR 012, erweitert 19.07.2026): Looper-/Track-Delete
  direkt nur ohne Clips UND ohne Patch-Out-Kabel; sonst X/OK-Dialog →
  Force-Delete in den `LooperTrashCan` (~180 s, ↺-Kachel): Clips
  DETACHEN statt deleteClip (Bank bleibt Besitzerin), Kabel
  spec-relativ sichern. Auch Einzel-Clip-Delete (Geste) läuft über den
  Papierkorb (`trashClipSlot`, Kind `clip` — spielend erst stoppen).
  Einträge haben stabile `entryId`; ↺ bei mehreren Einträgen =
  Auswahl-Liste (`LooperTrashDialog`), Restore gezielt via
  `restoreLooperTrashEntry`. Thumbnails parkt der Editor nach clipId
  (`stashLooperThumbnails` VOR dem Detach, Re-Apply im
  refreshLooperStatus, Purge bei trash.onChanged). prepareToPlay:
  `trash.clearWithoutDelete()` VOR `bank.prepare()`. Kein UndoManager
  für Clips/Struktur.
- Quellen-Combo listet NUR Looper-patch-IN-Slots (zuoberst) + Interface-
  Eingänge (Mono/Stereo folgt dem ∥-Pairing der ChannelNames).
- Slot-Namen/-Farben (19.07.2026): autoName = SIGNALKETTE „Quelle ·
  FX…" (resolveSourceChainLabel; refreshLooperPatchInAutoNames bei jeder
  Kabel-Änderung; userName gewinnt, „ 2"-Suffix bei Kollision); Renames
  migrieren gespeicherte tap:-Keys (`CaptureService::onChannelRenamed`).
  Slot-/Waveform-/Clip-Farbe = geerbte Quellfarbe
  (`Core/SignalFlowColours`, geteilt mit dem NodeCanvas), Fallback
  nodeColour.
- looper_patch_in-Default = 4× stereo + 4× mono (12 Kanäle) — exakt die
  CaptureService-Slot-Reserve. Looper starten ab Werk OHNE Quelle:
  dauerhaft gearmte Kanäle (alter „master"-Default!) blockieren die
  aufgeschobene Puffersatz-Erweiterung für immer.
- **Track-Mixer + Distanz (ADR 015, 20.07.2026):** Send-LEVEL (0..1,
  5-ms-Block-Ramp) ersetzen die Bitmaske; `getTrackSends` ist nur noch
  ABGELEITET (Bit = Level > 0). Distanz-Zug NACH Gain/Pan/Mute, VOR
  Meter/Post-Bus; Post-Sends greifen NACH Filter+Width, aber VOR der
  Vol-Kurve ab (Y-Link-Send bleibt bei voller Distanz hörbar).
  Vol Dump default AN, bei d = 1 EXAKT Stille; `d_eff = y · ySens`.
  Koeffizienten 1×/Block auf dem Audio-Thread (`LooperDistance.h`, pure),
  Bypass-Guard bei d ≈ 0.
- **Wrap-Crossfade (Klick-Lektionen 20.07.2026):** Die Zone gehört ans
  FENSTER-Ende, nicht ans Content-Ende. Teilfenster blenden auf das
  hinter dem Fensterende weiterlaufende Material, das Vollfenster auf
  den Lead-in (bit-identisches Bestandsverhalten). Der Winkel läuft
  durch ein SMOOTHSTEP (equal-power bleibt exakt, Endpunkte mit
  Steigung 0) + 24-Sample-Vorlauf — sonst reißt der Restanteil des alten
  Materials beim Wrap ab (bei Varispeed verstärkt). Sehr kurze Fenster
  bekommen einen relativ längeren Fade (`wrapFadeSamples`, gemeinsamer
  Helfer von Renderer UND Apply-Logik).
- **Parameter NIE mitten in der Wrap-Blende anwenden** (Knack-Fund
  20.07.2026): dort hängt das ausklingende Material am Fenster-Ende —
  eine LEN/POS-Änderung reißt die Mischung auseinander (Faktor 1000
  über der Signalkrümmung). Ausgenommen sind bewusst getimte Wechsel
  (Reverse „Loop-Grenze"/„Quantized").
- **Der LooperSettings-Broadcast feuert pro MAUSBEWEGUNG** (Mixer-
  Gesten). Jeder Konsument in `applyLooperSettings`/
  `refreshLooperStructure` muss idempotent UND billig sein — sonst
  reißt er den Audio-Thread mit (Perf-Fund 20.07.2026:
  `LooperWaveformTap::setSource` bumpte blind die Version und hielt
  damit 8 Takte Backfill inkl. FFT dauerhaft am Laufen, DSP-Meter 99 %).
  Entschärft sind zusätzlich `setLooperStructure` (Early-Out),
  Hook-Verdrahtung/Quellen-Menü (nur bei echter Änderung) und die
  XML-Serialisierung der Mixer-Setter (250-ms-Bündelung, Broadcast
  bleibt sofort).
- **MIDI-Map (ADR 016):** eigene `grid::MidiInBindings`-Instanz
  (`LooperMidiMap`, Datei `Conduit/LooperMidi.settings`), Ziele als
  gepackter controlId auf `kLooperLayer = 2` (`LooperMidiTargets.h`,
  pure). Klassisches Learn; Eingänge von ALLEN Controller-Rolle-Geräten
  über den 60-Hz-Hub-Drain (Audio-Thread nie beteiligt). Dispatch
  ausschließlich über die vorhandenen UI-Hooks — keine zweite Wahrheit.
  Das MAP-Overlay MUSS einen Weg hinaus lassen (Panel fängt nur über
  Zielen, Transportleiste frei, ESC beendet).
