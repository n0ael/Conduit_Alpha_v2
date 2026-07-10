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
  - **Ränder (Grid-Page v2, Ribbon-Umbau 07/2026):** ExpressionRibbon ×3,
    alle bipolar (Mitte neutral, ungeklemmter Rohwert hält den vollen
    Bereich) — links „Pitch" in voller Höhe (±12 HT, setPitchBendOffset,
    grün), rechts eine 72-px-Spalte mit „Pressure" oben (setPressureOffset,
    orange) über „Slide" unten (setSlideOffset, cyan); Füllfarbe pro Achse
    via ExpressionRibbon::setFillColour (aus GridPanelSettings, s. u.).
    Das M1-Volume-Ribbon ist entfallen —
    GridVoiceEngine::setGlobalVolume (Master-CC7) bleibt für
    Tests/Zukunft. Release-All → GridVoiceEngine::allNotesOff.
  - **Achsen-Farben (Grid-Page v2, 07/2026):** Farbe pro Achse
    (Pressure/Slide/PitchBend) user-konfigurierbar, persistent in
    GridPanelSettings (Hex-String-Keys `axisColour{Pressure,Slide,
    PitchBend}`, Defaults ledOrange/ledCyan/ledGreen). Quelle der Wahrheit
    für MpeShapingView (Kurve/Label/Endpunkte/Noten-Kreise + LockToggle-
    Akzent) UND die ExpressionRibbon-Füllungen (GridPage liest initial aus
    den Settings, live via MpeShapingView::onAxisColourChanged). UI: pro
    Detailspalten-Sektion unterster Punkt „Color" — 5 Quick-Swatches
    (LED-Tokens; Tap = sofort, Gedrückthalten ~450 ms = ConduitColorPicker
    als CallOutBox, live). ConduitColorPicker (Source/UI) ist app-weit
    wiederverwendbar: SV-Fläche + Hue-Slider + 8×5-Preset-Raster,
    interner Zustand HSV (Hue bleibt bei s=0/v=0 erhalten), HSV↔RGB als
    pure statics (h01 in [0,1), Roundtrip 8-bit-exakt, Catch2-getestet).
  - **CC-Baukasten (Grid-Page v2, 07/2026):** zweiter Tab „CC" im
    EditorDockPanel (CcPanel: Werkzeuge Fader/Push/Toggle/XY) + CcControlLayer
    als Overlay exakt über dem Pad-Raster (GridPage: nach keyboard, identische
    Bounds). CC-Modus = Panel offen UND aktiver Tab „cc"
    (EditorDockPanel::onActiveTabChanged/getActiveTabId → GridPage::
    updateCcMode → CcControlLayer::setCcMode): Drag mit Werkzeug zieht ein
    Control auf dem Zellraster auf (CcControlModel, Source/Core — UI-frei,
    Catch2-getestet), Drag verschiebt grid-snapped (moveTo klemmt), ×-Zone
    entfernt; ohne Werkzeug schluckt der Layer Events (keine Noten im
    CC-Modus). MPE-Modus: freie Flächen fallen per hitTest zum Keyboard
    durch (Pads UNTER Controls stumm), Controls multi-touch bedienbar
    (Fader vertikal, Push halten, Toggle-Tap, XY-Drag). Wertänderungen sind
    vorerst reiner UI-State — der MIDI-CC-Versand dockt später an
    (CcControlLayer::onControlValueChanged, TODO(design)); ebenso offen:
    CC-Nummern-Zuweisung pro Control.
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
