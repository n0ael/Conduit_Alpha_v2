# Grid-Touch-Controller (Ω) — Subsystem-Dossier
> Ausgelagert aus CLAUDE.md v4.6 §10.0, Juli 2026. Für Arbeiten an
> diesem Subsystem verbindlich wie die CLAUDE.md selbst (§1.1).

- **Grid-Touch-Controller (Ω, M1 07/2026 — MPE-Keyboard als erstes Modul des
  Baukastens; symmetrisches Rückgrat Quelle → Voice-Modell → Sink, ADR 14):**
  - **Kette:** GridKeyboardComponent (Touch) → GridVoiceEngine (Engine-Level
    wie Looper/Metronom, EngineProcessor-besessen, Message-Thread — kein
    Audio-Thread/Graph) → IVoiceSink → MpeMidiSink (MPE-MIDI 1.0, Lower Zone)
    → IMidiOutputTarget → MidiDeviceTarget (öffnet nur EXISTIERENDE MIDI-Out-
    Ports, erzeugt keinen eigenen virtuellen Port — plattformabhängig).
  - **Voice-Modell:** VoiceAllocator (Finger → Voice-Slot, allocation-free,
    thread-agnostisch, Stealing = ältester) + MpeEncoder (Voice-Slot-Event →
    juce::MidiMessage; Member-Kanäle ab 2, Master-Kanal 1). MPE = MIDI 1.0.
  - **Touch:** PadGridLayout (isomorph, +1 HT/Spalte, +5/Reihe; Note beim
    Aufsetzen, Pitch-Bend aus X, Ausdruck aus Y — ungeklemmt/aufsetzpunkt-
    relativ, einstellbare yRangeNorm, Clamp nur am Ausgang). RingTouchModel
    (Sonne = primärer Finger; Mond = zweiter Finger im Orbit-Greifband,
    Radius → Slide; Orbit friert relativ zur Sonne ein, wandert mit ihr,
    weit weg wieder greifbar).
  - **Ränder:** ExpressionRibbon ×2 — Volume (unipolar → Master-CC7) und
    AT-Offset (bipolar, Mitte neutral → interner Pressure-Offset auf jede
    Stimme; ungeklemmter Rohwert hält den vollen Bereich). Release-All →
    GridVoiceEngine::allNotesOff.
  - **Sinks/Stränge später:** OSC (Remote + Transcoder) und CV (Software-CVC)
    docken am selben Voice-Modell an; Gesten-State-Machine (Drone/Latch/
    Pinch/Drift), Chord-Squares, Hardware-MPE-Input, MPE-Shaping (Kurven +
    Slide/PitchBend-Offset) als eigene Stränge (Roadmap 11).

## Meilensteinleiter (Roadmap §11)

  M1  Voice-Engine + direkter MIDI-Sink + spielbares 2-Stimmen-MPE-Keyboard (Circle-Mechanik, Release = Finger heben, Rand-Ribbons, Release-All) — erledigt 07/2026 — 10.0
  danach unabhängig, Reihenfolge nach Priorität:
    - OSC-Sink + Transcoder (Remote, cross-platform)
    - Gesten-State-Machine (Drone/Latch per Abhebe-Reihenfolge, Pinch-weg, Doppeltipp, Drift-über-Rand-und-Faden)
    - CV-Sink (Software-CVC)
    - Hardware-MPE-Input (macht Conduit zum Hub; mit CV-Sink = Haken CVC in Software)
    - Chord-Squares + Save/Load (Browser, Factory-Sets zum Losjammen ohne Theorie)
    - Omnichord-Strings
