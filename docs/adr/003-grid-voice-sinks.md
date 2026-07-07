### ADR: Grid-Voice-Ausgabe — Quelle → Voice-Modell → austauschbare Sinks

- **Rückgrat:** Grid = Touch-Controller-Baukasten, erstes Modul = MPE-Keyboard.
  Symmetrisch: mehrere Quellen (Grid-Touch, später Hardware-MPE-Input) → EIN
  internes Voice-Modell → mehrere austauschbare Sinks (MIDI, OSC, CV) hinter
  Interfaces (IVoiceSink Sink-Seam, IMidiOutputTarget MIDI-Sende-Seam, Muster
  IOscSink 7.3).
- **MPE-Zuteilung liegt IN Conduit** (VoiceAllocator + MpeEncoder, testbar);
  MPE ist MIDI 1.0, kein MIDI-2.0-Bedarf. Ein späterer OSC-Sink trägt bereits
  zugeteilte Voices → externer Transcoder bleibt logikfrei (Leos Max-Domäne).
- **Thread-Ebene:** reine MIDI-Ausgabe berührt weder Audio-Thread noch Patch-
  Graph — GridVoiceEngine ist Engine-Level (Muster Looper/Metronom), Message-
  Thread. IPolyphonic (4.2, Audio-Thread) bleibt dem späteren CV-Sink vorbehalten.
- **Ausdrucks-Achsen ungeklemmt an der Quelle, Clamp nur am Ausgang:** so bleibt
  trotz globalem Offset der volle Bereich durch Weiterwischen erreichbar (Y
  umgesetzt; Slide/PitchBend folgen im MPE-Shaping-Strang).
- **MIDI-Out-Sink ist neue Infrastruktur** (4.1 kennt kein MIDI-Out-Modul);
  MidiDeviceTarget öffnet nur existierende Ports — eigenen virtuellen Port
  erzeugen ist plattformabhängig (Windows via Windows MIDI Services/Loopback,
  macOS nativ; eigener Strang).
- **Terminologie:** Sonne (primärer Finger) / Mond (Ring-Finger) / Orbit (Kreis)
  durchgängig in Kommentaren/UI; öffentliche API bleibt `ring*`/`primary*`.
