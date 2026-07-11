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
  - **Pad-Layout-Modi + Grid-Symbole (User 10.07.2026):** das Pad-Raster der
    Grid-Page ist 8×8 (64 Pads, Push-Style; GridPage::padLayoutConfig —
    die PadGridLayout-Config-Defaults bleiben 8×4, lowestNote 48 unten
    links, die Reihen wachsen nach oben). Zwei neue PushIcons aus User-SVGs
    (44er-ViewBox, Inhalt zentriert hochskaliert wie pageTouchLive):
    `gridMpe` (5×5-Punktmatrix — ersetzt das Ω als Grid-Tab-Symbol,
    pageGrid bleibt im Enum) und `gridMpeXy` (halbes Raster + XY-Block +
    Fader-Block, Aussparungen per Even-Odd-Füllregel). Zwei IconTiles in
    der Top-Row (links neben Release-All) schalten den Modus um, persistent
    als `gridLayoutMode` in GridPanelSettings (0 = fullPads, 1 = xyFaders).
    XY+Fader-Modus: ein eigener systemLayer (CcControlLayer über
    systemCcModel, 8×2-Zellraster, IMMER Play-Modus — kein Aufziehen/
    Verschieben/Löschen) überdeckt die oberen zwei Pad-Reihen mit fester
    Bestückung `grid::buildXyFaderLayout` (1× XY über (0,0)-(1,1) + 6
    vertikale Fader, alle 16 Zellen abgedeckt); das 8×8-Noten-Mapping
    bleibt unverändert, die überdeckten Pads sind unspielbar. Der
    systemLayer liegt ÜBER dem User-ccLayer und gewinnt dessen Hit-Tests
    auch im CC-Tab-Modus (System-Controls dort spielbar — akzeptiert).
    TODO(design): Anzahl/Anordnung + CC-Zuweisung der System-Controls,
    Persistenz der Control-Werte über den Moduswechsel.
  - **Akkord-Speicher (Grid-Page v2, Feature 6, 07/2026):** 8 vertikale
    „LCD-Screens" (ChordMemoryStrip, push::colours::lcdScreen) zwischen
    Pad-Raster und rechter Ribbon-Spalte; ChordMemory (Source/Core, UI-frei,
    Catch2-getestet) hält pro Slot eine Sonnen-/Mond-Konstellation
    normalisiert (x über Flächen-Breite, y über -Höhe, Mond-Offset ox/oy
    BEIDE über die Breite — Orbit bleibt beim Rescale rund). MPE-Modus:
    Tap auf leeren Slot speichert die aktuelle Konstellation (live +
    latched), Tap auf belegten Slot ruft sie „latched" aufs Grid ab
    (GridKeyboardComponent::latchConstellation — synthetische fingerIds ab
    0x10000, noteOn + Startwerte + Slide aus dem Mond-Offset; Sonnen
    außerhalb des Rasters nur visuell) und Ziehen verschiebt den Akkord
    starr (moveLatchedBy: X = Pitch-Bend, Y = Ausdruck wie ein Finger-Drag,
    kein Clamping). CC-Modus: Tap löscht den Slot (belegte Slots werden nie
    überschrieben). Release-All beendet auch den latched Akkord
    (clearLatched). Mini-Ansicht pro Slot: Sonne 6 px/Mond 4 px ledWhite +
    Orbit-Ellipse (y-Radius über den Spielflächen-Aspekt gestaucht).
    Offen: Persistenz der Slots (TODO(design)), Save/Load-Browser +
    Factory-Sets (Meilensteinleiter).
  - **Sensitivity + Bend-Range (Masterplan Block A, 07/2026):**
    NumberFieldBracket (Source/UI, wiederverwendbares Zahlenfeld: Swipe,
    Doppeltipp = Default, eckige Klammern in Akzentfarbe) trägt je ein
    Sensitivity-Feld (0–100, 50 = Basisverhalten, `GridSensitivity.h`:
    Faktor 2^((s-50)/25) MULTIPLIZIERT die Geometrie-Spanne) oben in der
    Pressure-/Slide-Detailspalte der MpeShapingView; PitchBend bekommt dort
    den BendRangeSelector (¼ ½ ×1 ×2 ×4 ×8, multiplikativ auf
    `semitonesPerPadWidth`). Neue Laufzeit-Setter: PadGridLayout
    (setYRangeNorm/setSemitonesPerPadWidth), RingTouchModel (setRadiusRange),
    Durchreiche GridKeyboardComponent (immer von gecachten BASIS-Werten
    multiplizieren, nie akkumulieren). Laufzeit-only — Persistenz kommt
    gebündelt (Masterplan Block K). Die Dev-Werte Schwellbreite/Fade-Zeit
    wohnen seit Block A4 im floating DevPanel; MpeShapingView pollt sie
    live in tick() (GridPanelSettings ist bewusst kein ChangeBroadcaster).
  - **Pitch-Korrektheit + In-Tune + Expression-Modes (Block B, 07/2026):**
    B3-Kalibrierung: `semitonesPerPadWidth`-Default 2.0 → **1.0** — n
    Spalten Wischweg = n Halbtöne, aufs isomorphe Raster ausgerichtet
    (User-Befund: C2 + 8 Pads gewischt ergab +14 statt +8; Regressionstest
    „Wisch über n Spalten == Re-Hit derselben Position"). B1 In-Tune
    Location (grid::InTuneLocation, Default **pad** = Push-Paradigma:
    Pad-Zentrum in tune, Finger bendet ABSOLUT — Re-Hit = identischer
    Pitch; finger = Aufsetzpunkt 0 Bend als Option). B2 In-Tune Width in
    Pad-Prozent (Config `inTuneWidthPercent`, Default 20, TODO(design)
    Feinabstimmung): `PadGridLayout::pitchBendFromAnchor` = stetige
    Treppen-Kennlinie (Totzone um jedes Pad-Raster, Nachbar-Zentrum exakt
    ±1 Schritt; Zone 0 = linear). Der Akkord-Latch (moveLatchedBy) bleibt
    bewusst LINEAR (pitchBendSemitones). B4 ExpressionMode
    (MpeEncoder/MpeMidiSink): mpe (Kanalspreizung, heute) /
    polyAftertouch (EIN Kanal, Pressure als 0xA0 pro Note — Note liefert
    activeNote des Sinks) / monoAftertouch (EIN Kanal, 0xD0);
    `MpeMidiSink::setExpressionMode` beendet zuerst hängende Noten (alte
    Kanalzuteilung). UI-Exposition von B1/B2/B4 folgt im Settings-Tab
    (Block D), Persistenz in Block K.
  - **Mehrpunkt-Kurve (Block C, 07/2026 + Feedback-Runde 11.07.):** der
    MPE-Kurveneditor kann jetzt 2↔3 Punkte. Gesten: Zwei-Finger-DREHUNG =
    STUFENLOS (User-Feedback: kein An/Aus-Toggle) — der Drehwinkel steuert
    live die gegensinnige Bauchigkeit (Segment 0 = +v, Segment 1 = −v;
    volle Bauchigkeit bei 90°, Uhrzeigersinn = steile Mitte); beim
    Überschreiten der Totzone (|v| ≥ 0.08) erscheint der Mittelpunkt,
    zurück in die Totzone gedreht verschwindet er wieder (die vorherige
    2-Punkt-Krümmung wird restauriert); eine neue Geste setzt am Ist-Wert
    an (kein Sprung), eine verschobene Mitte bleibt stehen. Drehen nur bei
    weit aufgeklapptem Panel (≥ editorThresholdWidth), Klassifikation
    Drehung (≥ 10°) vs. Zentroid-Drag (≥ 0.04 norm) — was zuerst die
    Schwelle reißt, gewinnt die Geste. Zwei-Finger-Drag ODER Ein-Finger
    auf dem Griff = Mittelpunkt verschieben; Ein-Finger-Krümmungs-Wisch
    wirkt links/rechts vom Mittelpunkt auf Segment 0/1; 3 Finger 2 s
    halten = Reset auf 2-Punkt-Identität (inkl. Range). Headless-Helfer in
    `CurveEditInteraction` (Target::MidPoint, curvatureSegmentAt,
    rotationDegrees, degreesToShapeAmount, applyShapeAmount,
    applyMidPointDrag, resetToDefault) — Wiederverwendungs-Seam fürs
    Macro-System (Block E); die Component-Extraktion (CurveEditorTile)
    folgt dort. Endpunkt schlägt Mittelpunkt schlägt Krümmung;
    Mittelpunkt-Griff = hohler Ring.
  - **Settings-Tab + Performance-Layout-Umbau (Block D, 07/2026):** dritter
    Tab „Settings" im EditorDockPanel (`GridSettingsView`, selbständig --
    eigener ValueTree-Listener für die Skala-Kacheln, alles andere über
    Callbacks wie MpeShapingView/CcPanel). Inhalt: Performance-Slide-Out
    (MIDI-Ausgangsport + Session-Skala-Kacheln, umgezogen aus der
    ehemaligen GridPage-Top-Row), In-Tune Location (Pad/Finger, Block B1)
    + In-Tune Width (Block B2, `PadGridLayout::setInTuneWidthPercent`),
    Expression Mode MPE/Poly-AT/Mono-AT (Block B4, direkt an
    `MpeMidiSink::setExpressionMode` -- neuer `EngineProcessor::
    getMpeMidiSink()`-Getter + `GridPage`-Ctor-Parameter, da `IVoiceSink`
    bewusst keine MPE-Spezifika kennt), Layout-Feinabstimmung (XY-Zeilen
    + gemeinsame Fader-Breite als `NumberFieldBracket`-Zahlenfelder statt
    der in der Roadmap beschriebenen freien Drag-Resize-Fläche --
    TODO(design): die eigentliche Drag-Interaktion folgt separat, die
    Werte sind bereits voll wirksam/persistent in `GridPanelSettings`
    (`systemControlRows`, `ribbonWidthPx`, sofort persistent wie
    `gridLayoutMode`, nicht erst Block K), Modwheel-Toggle (`modwheelEnabled`,
    unipolares ExpressionRibbon neben Pitch, sendet CC1 direkt über
    `MidiDeviceTarget.send()` auf dem MPE-Master-Kanal -- kein eigener
    Sink-Pfad). MIDI-Kanal-/CC-Zuweisung der Performance-Controls (Port/
    Kanal/CC-Matrix, Empfang) bleibt TODO(design)/Block G -- es gibt
    aktuell keine MPE-Kanal-Wahl-Infrastruktur (`memberChannelBase` ist
    `MpeEncoder::Config`-fix).

    `systemControlRowsAtStartup` (GridPage): `CcControlLayer::rows` ist
    `const` (kein Laufzeit-Resize) -- ein geänderter XY-Zeilen-Wert
    persistiert sofort, wirkt aber erst beim nächsten Neuaufbau der
    Grid-Page (TODO(design): echtes Laufzeit-Resize braucht
    CcControlLayer-Umbau). Die Ribbon-Breite dagegen ist voll live (reine
    `setBounds`-Geometrie in `resized()`, kein Konstruktions-Fixpunkt).

    Performance-Layout (Block D2): Release-All-Button jetzt UNTER dem
    Pitch-Ribbon, zwei neue Oktav-Buttons („Oct +"/„Oct -") DARÜBER
    (`GridKeyboardComponent::octaveUp/octaveDown`, ±`kMaxOctaveShift`=3
    Oktaven, gecachte `baseLowestNote`-Basis wie die Block-A-Sensitivity-
    Setter -- nie vom aktuellen Config-Wert weiterspringen). Die
    Layout-Modus-Kacheln (64 Pads/XY+Fader) sitzen jetzt kompakt oben
    links (28-px-Streifen) statt neben Release-All.
  - **Macro-System (Block E, 07/2026):** Long-Press (~450 ms ohne
    nennenswerte Bewegung, `CcControlLayer` + Timer, Drag bricht ab) auf
    einem Control im Play-Modus öffnet den vierten Dock-Tab „Macro"
    (`MacroPanel`) mit der Ziel-Liste des Controls. Bis zu 16 Ziele pro
    Control-WERT (`MacroBindings`, keyed nach Layer[system/diy] +
    controlId + Achse — XY-Pads haben zwei Achsen, X/Y-Umschalter oben;
    y wird für Macro-Semantik invertiert, oben = 1). Ziel-Typen gemischt
    (final): `MidiCcTarget` (Kanal + CC über den Grid-MIDI-Ausgang,
    7-bit-Dedupe — Glättung ist Sache der Zielschicht) und
    `AbletonParamTarget` (Track→Device→Parameter-Dropdowns aus dem
    LiveSetModel; Versand exakt wie TouchLiveDeviceView::sendParameter:
    noteTouchedParameter + `/live/device/set/parameter [dvid,index,value]`
    über den 16-ms-Fast-Path, Wert nativ in [min,max] aus parmeta,
    quantisiert gerundet; dvid = Laufzeit-ID, NIE serialisieren —
    Block-K-Persistenz braucht Namens-Re-Resolve, nach onReconnected
    potenziell stale, TODO(design)). Pro Ziel ein `CurveEditorTile`
    (Component-Extraktion der Block-C-Helfer: Endpunkte + Mittelpunkt-Drag
    + Segment-Krümmung, OHNE Offset/Drehgeste — Mini-Format);
    `MacroBindings::applyValue` klemmt den Kurvenausgang auf [0,1].
    Compact-View: gewählte Zeile groß (Editor), übrige eingeklappt als
    Linie mit Punkt (zuletzt gesendeter Wert, VBlank-Poll) + Min/Max-
    Strichen; „+ Ziel" ergänzt Slots, Liste scrollt (Viewport).
    `onControlValueChanged` beider Layer (System + DIY) ist damit
    verdrahtet (GridPage::feedMacros). Offen: CC-Funktionsnamen aus der
    Geräte-Datenbank (Block L), Bindings-Invalidierung bei Control-Löschung
    im Edit-Modus (Ids recyceln erst bei clear() — TODO(design)).
  - **DIY-Tab (Block F, 07/2026):** der „CC"-Tab heißt jetzt sichtbar
    „DIY" (Tab-ID bleibt "cc" — updateCcMode hängt daran). VERSCHIEBEN von
    Controls im Edit-Modus ist frei statt zell-gesnappt: `CcControl` trägt
    einen normalisierten freien Rect (rx/ry/rw/rh, rw<=0 = noch aus Zellen
    abgeleitet — Aufziehen/GRÖSSE bleibt Raster-basiert), der Layer
    initialisiert ihn beim ersten Griff. Snapping über die headless-Klasse
    `Source/Core/FigmaSnap` (Catch2-getestet): Mittel-Achsen-Flucht zu
    anderen Controls + Gleichabstand (Paar-Mittelpunkt und Verlängerungen
    einer Zweierreihe), pro Achse unabhängig, Schwelle 8 px; cyan
    Guide-Linien während des Drags; kein Rast-Zustand — kleines Weiterziehen
    über die Schwelle löst die Flucht (Snap rechnet immer von der rohen
    Zeigerposition). Hit-Tests (Play + Edit + hitTest-Durchfall) sind jetzt
    rect-basiert (controlIdAt, oberstes zuerst) statt zell-basiert.
    Zuweisung läuft über die Macro-Ansicht aus Block E (Long-Press).
  - **MIDI-Input für Controls (Block G, 07/2026):** externe CC-Eingänge
    bewegen Conduit-Fader/XY (Anzeige folgt) OHNE Parametersprung.
    `MidiControlInput` (Core): öffnet existierende MIDI-In-Ports; der
    JUCE-MIDI-Callback (System-Thread!) pusht CC-Events wait-free in eine
    `SpscQueue`, ein 60-Hz-Timer pumpt auf den Message Thread.
    `MidiInBindings` (Core, headless, Catch2): pro Control-Wert
    (MacroControlKey) EIN Eingangs-CC (bind ersetzt Key- UND CC-Kollision);
    `SoftTakeover` = Pickup bei Kreuzung des Ist-Werts oder Nähe (ε 0.03),
    lokaler Touch löst (notifyLocalTouch in den Layer-Callbacks — der
    externe Fader muss neu aufnehmen); One-Pole-Eingangs-Glättung
    (0.35/Tick, Snap 0.004) macht aus 127-Stufen-CCs weiche Fahrten, die
    über GridPage::applyExternalValue auch die Macro-Ziele (→ OSC/Live)
    weich erreichen. UI: MIDI-Eingang-Combo im Settings-Slide-Out,
    MIDI-In-Zeile (Toggle + Ch/CC) im Macro-Panel pro Control/Achse.
    LED-Feedback/Motorfader: NUR Schnittstelle
    (`MidiInBindings::onFeedbackEcho(channel, cc, value01)`, feuert beim
    Anwenden — Hardware-Implementierung später). Laufzeit-only (Block K).
  - **Grid-Page-Button: Mode-Toggle + Track-Fokus-Routing (Block H +
    v2-Feldtest-Runde, 07/2026):** die Page-Tiles der TransportBar sind
    `push::HoldIconTile` (wiederverwendbar: onClick bleibt der Tap-Hook,
    onLongPress nach 450 ms ruhigem Halten, Bewegung > 8 px bricht ab;
    Kernpfade beginPress/movePress/endPress/firePressTimeout, KEINE
    Button-Klick-Maschinerie — setState für die Down-Optik). Tap auf den
    Grid-Tile bei SCHON aktiver Grid-Page toggelt `gridLayoutMode`
    (64 Pads ↔ XY+Fader, `GridPage::toggleLayoutMode`; Entscheidung im
    EngineEditor, Vorher-Zustand nötig); das PAGE-ICON zeigt den Modus
    (gridMpe ↔ gridMpeXy via `GridPage::onLayoutModeChanged` +
    `IconTile::setIcon`) — die früheren Modus-Kacheln oben links sind
    ENTFALLEN. Dort sitzt jetzt das `TrackFocusBadge` (Source/UI,
    wiederverwendbar): Live-Farbe + Name des Tracks, den das Grid
    spielt (tracks-Domain-Key `conduit_focus`, folgt bewusst NICHT
    Lives Selektion). Unten rechts ein Arm-Button (Oktav-Button-Maße)
    für den Fokus-Track (LED aus der mixer-Domain).

    Long-Press öffnet den `TrackSelectorPanel` (CallOutBox am Tile):
    oben ein „Follow Selection"-Toggle (User 11.07.2026: dort, damit
    entdeckbar; persistent `GridPanelSettings::midiFollowSelection`),
    darunter die MIDI-Tracks mit Name + Live-Farbe (Fokus markiert).
    Auswahl sendet `/live/song/set/midi_input_focus [trackKey,
    gridInput, masterInput, follow]` — gridInput =
    `MidiDeviceTarget::currentDeviceName` (Portname, beim User
    „Conduit Grid MPE"), masterInput = `GridPanelSettings::
    masterMidiInputName` (Dropdown im Settings-Tab aus dem
    tracks-Domain-Key `input_options`, z. B. „FromPush").

    **Routing-Semantik v2 (Live-Performance: Grid und Push spielen
    GLEICHZEITIG verschiedene Tracks), `sync/inputfocus.py`:**
    Ziel-Track Input = gridInput + Monitor In; alle anderen
    All-Ins-MIDI-Tracks Monitor OFF (Auto würde Grid-Noten bei Arm
    durchlassen — All Ins enthält den Conduit-Port!); Lives
    selektierter Track (MIDI + All Ins) → masterInput + Auto. Follow
    Selection (selected_track-Listener, nur mit aktivem Fokus): neu
    selektierter All-Ins-Track → masterInput + Auto, der vorher
    bewegte zurück auf All Ins + OFF; der Fokus-Track bleibt IMMER
    unangetastet; ein ALTER Fokus wird nur restauriert, wenn sein
    Input noch der Grid-Port ist (User-Umroutings nie überschreiben).
    „All Ins" = Eintrag 0 der available_input_routing_types (nie der
    lokalisierbare String); Identitäten über `_live_ptr`
    (stable_ids._identity); Fokus-/Follow-Zustand lebt NUR in der
    Script-Session. tracks-Domain-Skalarkeys: `selected`,
    `conduit_focus`, `input_options`. **Deploy-Falle (Feldtest
    11.07.2026):** das Script läuft als KOPIE in Lives User Library
    (`robocopy Tools\Live\ConduitRemote → …\Remote Scripts\ConduitRemote
    /MIR /XD __pycache__ tests`) + Live-Neustart — sonst „passiert
    nichts in Ableton". Folgeschritt (User): MPE-MIDI-In-Noten-Echo
    (Pad-Glow in Track-Farbe, ohne Sonne/Mond).
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
    - Chord-Squares + Save/Load (Browser, Factory-Sets zum Losjammen ohne Theorie) — Akkord-Speicher (8 LCD-Slots, Abruf + starres Verschieben) erledigt 07/2026; Save/Load-Browser + Factory-Sets offen
    - Omnichord-Strings
