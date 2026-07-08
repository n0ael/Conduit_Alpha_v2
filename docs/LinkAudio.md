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
## Empfangen (Phase 2) — Spezifikation (Design 08.07.2026)

Referenz: SDK-Beispiel `examples/linkaudio/LinkAudioRenderer.hpp`
(linkaudiohut) — dessen Muster übernehmen wir mit Conduit-Bausteinen.

- **Kette:** `LinkClock::Source`-Callback [Link-Thread] stempelt
  `beginBeats(sessionState, quantum)` sofort beim Empfang (macht der
  Wrapper bereits) → POD-`ReceiveSlot` (rohe int16-Samples + numFrames/
  numChannels/sampleRate/beatBegin/tempo/count) → `SpscQueue<ReceiveSlot>`
  (Producer = Link-Thread, Consumer = Audio-Thread; einzige erlaubte
  Queue, 3.1) → Audio-Thread poppt pro Block in einen lokalen Slot-Ring
  (nur Audio-Thread, kein Atomic nötig) → `LinkReceiveStream` rendert.
  Der Link-Thread macht NUR memcpy — Int16→Float passiert im Audio-Thread.
- **Beat-Alignment (nie naiv FIFO'en, v1-Drift-Lektion):** Ziel-Fenster
  pro Block `targetBegin = clock.beatAtBlockStart − latencyBeats`,
  `latencyBeats = latencyMs/1000 · bpm/60`. Slot-Endbeat =
  `beatBegin + numFrames/sampleRate · tempo/60` (Peer-Tempo aus Info —
  exakt bei konstantem Tempo, Sprünge fängt der Reset ab). Zu alte Slots
  droppen, zu neue abwarten (Stille).
- **Re-Pitching statt Resampler:** `frameIncrement = totalFrames/numFrames`
  über die Slot-Kette, kubische Interpolation (4er-Cache) — deckt
  SampleRate-Differenz UND Tempoänderungen ab (SDK-Muster). Kontinuität
  über `startReadPos`; Underflow/zu neu/Beat-Sprung → Reset auf Stille
  und Re-Init am Ziel-Fenster. Übergänge Stille↔Signal mit 5-ms-Rampe
  declicken (Looper-Duck-Lektion).
- **Monitoring-Latenz:** Parameter `latency_ms` (Default 150, Bereich
  20–500, Echtzeit via Dual-State 6.1). Das SDK-Beispiel nutzt fix
  4 Beats (= 2 s bei 120 BPM) — zu träge fürs Monitoring; Same-Machine
  kommt deutlich niedriger aus. `buffered`-Sekunden als atomic für die
  UI (Tuning-Hilfe). Die effektive Hörlatenz gegenüber dem Peer IST
  latency_ms — dokumentierte Eigenschaft des Verfahrens, kein Bug.
- **Persistenz:** gespeichert wird NUR der Kanal-WUNSCH als
  `(targetPeer, targetChannel)`-Strings; `ChannelKey`s sind
  session-transient (CLAUDE.md 6, Phantom-Connection-Lektion). Re-Bind
  beim `ChannelsChanged`-Broadcast: Wunsch gegen `availableChannels()`
  matchen, eigene Sinks (peerName == eigener) ausfiltern.
- **Lifecycle:** Receive nutzt denselben `enableAudio`-Refcount wie Send.
  Delete Phase 1 (`releaseSessionResources`): `Source`-Reset auf dem
  Message Thread genügt — `~LinkAudioSource` setzt den SDK-Callback auf
  No-op (Member-Reihenfolge-Muster im Wrapper); danach schreibt kein
  Link-Thread mehr in die Queue. Kein Epoch-Handshake nötig (anders als
  beim Sink-Retire, wo der Audio-Thread Producer ist). Die Queue muss
  die Source überleben (Deklarations-Reihenfolge!).
- **Diagnose:** `Info::count`-Sequenzlücken zählen (Netzwerk-Drops),
  Underflow-/Reset-Zähler als Atomics für Dev-Statuszeile.
