# Link Audio (Audio in der Link-Session) — Subsystem-Dossier
> Ausgelagert aus CLAUDE.md v4.6 §7.2, Juli 2026. Für Arbeiten an
> diesem Subsystem verbindlich wie die CLAUDE.md selbst (§1.1).

- **LinkAudio ERSETZT Link** — beide Klassen nie parallel instanziieren
  (Header-Doku Link 4.0). `LinkClock`-Pimpl hält die einzige
  `ableton::LinkAudio`-Instanz für Timing UND Audio.
- **IWYU-Falle:** `<ableton/LinkAudio.hpp>` in JEDER Compilation Unit
  inkludieren, die LinkAudio-Typen berührt — die Link-Header sind nicht
  selbsttragend.
- **Format:** interleaved 16-bit signed int. Float→Int16 IMMER mit
  TPDF-Dither (LCG-basiert, kein rand()). Sink-Größe in SAMPLES anlegen
  (`samplesPerBlock * numChannels`) — Frames und Samples nie mischen.
- **Sinks senden erst, wenn mindestens eine Source subscribt** —
  Idle-Sinks sind gratis. UI unterscheidet „announced" vs. „streaming".
- **Threading:** `enableLinkAudio()`, `channels()`, Callbacks → Message
  Thread; `ChannelsChangedCallback` und Source-Callbacks kommen auf einem
  Link-Thread und werden via `MessageManager::callAsync` gemarshallt.
  `BufferHandle::commit()` ist RT-safe und nutzt dieselbe
  SessionState/Beat/Quantum-Basis wie das lokale Rendering (aus dem
  ClockState des Blocks — kein zweites captureAudioSessionState im Modul).
- **Sink-Lifecycle = zweiphasiges Delete:** Sink-Reset in Phase 1
  (Pattern OscController), sonst Zombie-Kanäle bei den Peers.
- **Kanal-Name = moduleId**, Rename via `sink.setName()` live propagiert.
- **Empfangen (Phase 2):** Buffer-Alignment über `BufferHandle::Info::
  beginBeats(sessionState, quantum)` — nie naiv FIFO'en (v1-Drift-Lektion).
