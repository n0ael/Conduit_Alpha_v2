# Looper (Retro-Looper + Vollausbau) — Subsystem-Dossier
> Ausgelagert aus CLAUDE.md v4.6 §10.0, Juli 2026. Für Arbeiten an
> diesem Subsystem verbindlich wie die CLAUDE.md selbst (§1.1).

- **Retro-Looper** (`Source/Core/Looper` + `Source/UI/LooperPage`, Stand
  07/2026 — Endlesss-Muster auf Capture-Audio-Basis, MVP = ein Loop):
  - Immer aufnehmend: Quelle = Capture-Kanal. Schlüssel seit Looper-I/O
    (ADR 010, 18.07.2026): „master" = Master-Output-Tap master_l/_r nach
    dem GraphFader (resolvet weiter, steht aber nicht mehr in der Combo) |
    „hw:{paar}" = gepaartes Eingangs-Paar | „hwm:{kanal}" = ungepaarter
    Mono-Eingang (∥-Pairing der ChannelNames entscheidet) | „tap:{name}"
    = virtueller Capture-Kanal eines Moduls (Looper-patch-IN-Slots
    „{moduleId}/{slotName}", Mono ohne Suffix ⇒ right = −1 ⇒
    1-Kanal-Clip; Stereo _l/_r). Der frühere out:{paar}-Zweig ist
    Legacy (resolvet zu −1). Arming (`CaptureService::setChannelArmed`)
    hält das Gate zwangsweise offen. **Auflösung folgt der Registry
    SYNCHRON** (`CaptureService::onRegistryChanged` →
    `applyLooperSourceArming`, Feldtest-Fix 19.07.2026): eine
    Re-Materialisierung des Looper-patch-IN-Moduls registriert seine Kanäle
    auf NEUEN Slots, weil die gearmten alten ihr Material als held
    binden — ohne den Hook zeigten die gespeicherten Looper-Indizes
    dauerhaft auf den toten Kanal (Stille bei unverändertem
    Combo-Eintrag). Der Hook feuert bei register/unregister/rename,
    prepare und der nachgeholten Puffersatz-Erweiterung (Guard-Tick). Wellenform/Spektrum tragen die
    Farbe der Quelle (Kanal-Farbe aus ChannelNames bzw. nodeColour;
    `LooperWaveformStrip::setSourceColour` tönt auch die Spektrum-LUT).
    Quellen persistieren in LooperSettings (sourceKey pro Looper),
    der Anker in TransportSettings (looperAnchor, −1 = Kein Master-Out).
  - **Quellen-Combo (Looper-I/O 18.07.2026, ersetzt die Fassung vom
    09.07.):** Liste = Looper-patch-IN-Slots ZUOBERST (gruppiert pro
    Modul-Instanz) + Interface-Eingänge (Mono/Stereo nach ∥-Pairing,
    Labels/Farben aus ChannelNames). Master/Out-Paare/fremde Taps sind
    bewusst raus — solche Signale loopt man per Kabel ins
    Looper-patch-IN-Modul. Auswahl über Item-IDs = Quell-Index + 1, NIE über
    Item-Indizes (Separatoren verschieben sie). Live-Refresh:
    CaptureService/ChannelNames-Broadcasts + Root-Tree-Listener
    (nodeColour, numInput-/numOutputChannels, Input-Namen,
    Connection-Kinder).
  - **Slot-Namen & Farben (User-Regel 19.07.2026, „Quellname zuerst"):**
    Slot-Label = `{name} · {moduleId} · mono|stereo`. Der Slot-autoName
    FOLGT der verkabelten Quelle bei jedem Stecken und zeigt die ganze
    SIGNALKETTE — Klangquelle zuerst, dann die FX-Stationen
    („mopho · galactic_1", `resolveSourceChainLabel`; Multi-Input-
    Stationen folgen ihrem ersten verbundenen Eingang, Zyklen kappen).
    `GraphManager::snapshotAutoName` mit followSource +
    `refreshLooperPatchInAutoNames` bei JEDER Kabel-Änderung (auch Upstream);
    Kollisionen bekommen „ 2"-Suffixe; expliziter userName gewinnt —
    anders als der Link-Send-Einmal-Snapshot. In der Clip-Zelle friert
    der Combo-Text ein ⇒ das Instrument steht dort zuerst. Der Rename wandert zur Registry
    (inputNameChanged → setVirtualChannelName); gespeicherte tap:-Keys
    MIGRIEREN dabei mit (`CaptureService::onChannelRenamed` →
    EngineProcessor mappt alt→neu in den LooperSettings). Slot-Farbe =
    Farbe der an den Slot verkabelten Quelle über die
    Signal-Flow-Vererbung (`Core/SignalFlowColours` — aus dem
    NodeCanvas herausgelöst, beide teilen dieselbe Logik), Fallback
    nodeColour des Moduls. Die Kette Eingang → FX → Slot → Waveform
    (`setSourceColour`) → Clip-Thumbnail (Zellfläche) ist damit
    durchgängig quellfarbig.
  - **Looper patch IN (ADR 010, umbenannt ADR 013 19.07.2026):**
    `LooperPatchInModule` (looper_patch_in, vormals looper_in;
    Browser-/Header-Name „Looper patch IN"): dynamische
    Mono/Stereo-Slots → Capture-Taps, Pass-Through; Slot-Umbau
    re-materialisiert gefadet; Default-Bestückung 4× stereo
    + 4× mono = 12 Kanäle, 19.07.2026 — die CaptureService-Reserve (12)
    deckt genau EIN Default-Modul; Slots jenseits der Reserve werden
    erst auflösbar, wenn kein Capture-Kanal aktiv ist [Guard-Tick].
    Deshalb starten die Looper ab Werk OHNE Quelle: der alte
    „master"-Default hielt die Master-Kanäle dauerhaft gearmt und
    blockierte die Erweiterung für immer. Das kompakte `LooperOutModule`
    (looper_out, „Looper Out Mini", Abgriffe Master|Looper × Modus ×
    Pre/Post) wurde mit ADR 013 ERSATZLOS ENTFERNT — zwei Out-Module
    waren verwirrend; Pre-Abgriffe/Mono-Modi deckt niemand mehr ab
    (Sends bieten Pre/Post pro Track). Engine-Seite:
    `LooperBank::renderBlock` VOR dem Graph, `getAudioView()` im selben
    Callback, `mixToOutput` (Master-Mix, additiv) NACH dem Graph;
    „sendMaster" pro Looper (LooperSettings) filtert NUR den Master-Mix.
  - **Looper patch OUT (ADR 012 „Big Looper Out", 19.07.2026; umbenannt
    ADR 013):** `LooperPatchOutModule` (looper_patch_out, vormals
    looper_big_out; Browser-/Header-Name „Looper patch OUT") = EINZIGES
    Looper-Ausgangsmodul.
    Stereo-Slots folgen AUTOMATISCH der Struktur: Track-Outs geflattet
    Looper-major (post-fader — Mono-Clips kommen über die Panning-Sektion
    stereo heraus) → Bus-Outs (Post-Bus je Looper) → Send 1–4 (immer
    alle 4, stabile Kanal-Indizes) → Master. Engine: `trackBus[4][4][2]`
    ERSETZT den geteilten Render-Scratch (die In-Place-Fader-Kette
    hinterlässt dort das Post-Fader-Signal), `sendBus[4][2]` mit
    pro-Track-Bitmaske + PRE/POST-Abgriff (LooperSettings
    `TrackState.sends`/`sendPre` → Bank-Atomics via applyLooperSettings;
    UI: SND-Kachel im Track-Strip → LooperSendDialog-CallOut).
    GraphManager: `setLooperStructure`/`syncLooperPatchOutConfigs` —
    Kabel-Remap über SPEC-IDENTITÄT (nie Kanal-Arithmetik: ein entfernter
    Track verschiebt die geflatteten Offsets aller späteren Slots),
    Re-Materialisierung EXPLIZIT anstoßen (bei gleicher Kanalzahl feuert
    kein numOutputChannels-Listener); Call-Sites applyLooperSettings,
    addModuleNode, loadPreset/setStateInformation, Force-Delete
    (synchron). Kachel read-only mit Sektions-Trennern
    (LooperPatchOutPanel), Ports fluchten via rowCentreY.
  - **Migration (ADR 013):** Alt-Schlüssel looper_in/looper_big_out
    werden beim Laden auf die neuen factoryIds umgeschrieben
    (`GraphManager::normalizeLoadedNodes` — läuft in
    setStateInformation/loadPreset VOR syncLooperPatchOutConfigs, plus
    normalizeNode bei jeder Materialisierung); moduleIds/tap:-Keys
    bleiben unverändert. looper_out-Nodes alter Patches laufen in den
    definierten nodeError-Pfad („Unbekanntes Modul") und sind löschbar.
  - **Delete-Gating + Papierkorb (ADR 012):** Looper-/Track-Delete nur
    noch direkt, wenn weder Clips noch Patch-Out-Kabel betroffen; sonst
    X/OK-CallOut (LooperDeleteConfirmDialog). OK = Force-Delete
    (`EngineProcessor::forceRemoveLooperTrack/-LastLooper`): Clips
    DETACHEN (`LooperSessionModel::detachSlot`, KEIN bank.deleteClip —
    Bank bleibt Besitzerin, ramBytesUsed zählt weiter), Kabel
    spec-relativ in den `LooperTrashCan` (~180 s), Struktur schrumpfen.
    ↺-Kachel im Looper-Header (`LooperTrashTile`) stellt den jüngsten
    Eintrag wieder her (`restoreLooperTrash`: Struktur nachwachsen,
    attachClip, Kabel aus der DANN gültigen Slot-Liste); letzte 30 s
    Rot-Fade, bei Ablauf kurzes Flackern. prepareToPlay leert den
    Papierkorb VOR bank.prepare (Store-Freigabe → Pointer wären
    dangling; kein Double-Free, da kein deleteClip aussteht). Der
    UndoManager bleibt außen vor (Clips engine-seitig, Struktur =
    App-Zustand).
  - `BarSampleAnchors` [Audio]: Taktgrenzen sample-genau als gepackte
    64-bit-Atomics (16 Bit bar-Tag + 48 Bit Sample-Position — Paar in EINEM
    Wort, sonst Slot-Reuse-Race); Grenze 0 wird nie überquert → Commit
    braucht bars+1 Grenzen.
  - Commit [MT] = letzte 8/4/2/1 KOMPLETTE Takte via Segment-Klick auf den
    gestauchten Waveform-Strip (Dichte verdoppelt sich an den
    Segment-Grenzen; beat-indizierte Min/Max-Bins, binsPerBeat 32); Kopie
    über das zählerbasierte Export-Halte-Protokoll, Wrap-Crossfade liest
    einen Lead-in VOR dem Loop-Start (5 ms equal-power).
  - Playback (`LooperEngine`, Engine-Level wie Metronom, bewusst ohne
    EngineProcessor-Abhängigkeit — späteres LooperModule hostet dieselbe
    Klasse): Phase beat-abgeleitet [B−L, B) → Start sofort phasenstarr,
    kein Drift; 2 Voices × 60 s (~46 MB @48 kHz), Re-Commit/Stop mit
    5-ms-Voice-Fades. Session- ≠ Aufnahme-Tempo ⇒ Varispeed (MVP-Grenze).
  - **Playhead-Lektion (3.1 bestätigt):** beatAtBlockStart ist
    Wall-Clock-basiert und jittert um den Callback-Scheduling-Versatz —
    NIE direkt als Lese-Basis nutzen (hörbare Körnung). LooperEngine führt
    einen sample-kontinuierlichen Playhead: Messung aus SampleClock +
    jüngstem Takt-Anker, Korrektur slew-limitiert (0.2 % Varispeed), Snap
    nur bei echten Beat-Sprüngen — und NIE hart (Feld-Lektion 04.07.2026:
    Link-Grid-Re-Syncs ließen die Messung pro Takt springen, jeder rohe
    Snap war ein Splice-Klick): Snap erst nach snapConfirmBlocks Blöcken,
    dann Duck-Declick (5-ms-Rampe auf 0 → Sprung unter Stille → zurück);
    snapCount als Diagnose in der Looper-Statuszeile („N Re-Syncs" —
    häuft er sich, wackelt Link-Achse oder Audio-Callback; Anzeige nur
    im Dev-Modus, UiSettings::devMode).
  - **Callback-Timing-Diagnose** (`Source/Core/CallbackTimingMonitor`):
    XRun-Zähler (Callback-Start-Gap > 2× Blockdauer = Deadline-Riss) +
    Load in ‰ des Block-Budgets als Durchschnitt UND Peak, gemessen um den
    GESAMTEN processBlock (QPC-Wall-Clock als dokumentierte 3.1-Ausnahme,
    NUR Diagnose, nie Zeitbasis). TransportBar zeigt „DSP x % ⌀ / y % pk ·
    N XRuns" rechts neben der Setup-Warnung — Durchschnitt = Ableton-CPU-
    Meter-Semantik, Peak = XRun-Frühwarner. EIGENER Settings-Schalter
    `UiSettings::dspMeter` (Default an), bewusst UNABHÄNGIG vom Dev-Modus
    (User-Entscheidung 04.07.2026). Peak-vs-Durchschnitt-Lektion: der
    Peak fängt den einen Block mit der Spektrum-FFT ein und liegt im
    Debug-Build ~10× über Release — CPU-Vergleiche NUR im Release-Build.
  - **Spektrum-View:** der Strip schaltet per Spectrum-Kachel (persistiert
    als looperSpectrum, TransportSettings) auf ein Spektrogramm um —
    zweiter always-on Tap-Pfad (FFT 2048/Hann, 64 Log-Bänder via
    looper::SpectrumBands, 16 Spalten/Beat), Rendering als
    ring-adressiertes Beat-Raum-Image + Segment-Blits (Fire-Palette);
    Segment-Klicks/Commit identisch in beiden Views.

- **Looper-Vollausbau (M1–M10, 07/2026 — ersetzt das Ein-Loop-MVP oben;
  dessen Lektionen [Playhead, Snap-Duck, Anker] gelten unverändert):**
  - **Struktur:** bis 4 Looper (eigene Quelle + eigener WaveformTap) ×
    bis 4 Tracks × 12 Clip-Slots (sichtbar 4–12 im Menü), Endless-Modell
    (immer-aufnehmend, Segment-Klick committet 8/4/2/1 Takte in den
    Target-Slot). Ein spielender Clip pro Track (Session-Verhalten).
  - **Engine:** `LooperBank` (ersetzt LooperEngine) — Voices REFERENZIEREN
    right-sized `LooperClip`s (RAM-Konto statt Prealloc, Default 1,5 GB);
    MT→Audio via SpscQueue<ClipCommand>, Audio→MT via Retire-Queue
    (Delete wandert IMMER durch den Audio-Thread, free nie im Callback,
    exportPins verzögern die Freigabe). Clip-Parameter als Staged/Active-
    Protokoll: der Audio-Thread wendet mit SEINEM Playhead positions-
    kontinuierlich an (LooperClipMath, geschlossene Re-Anker-Formeln);
    inhärente Lese-Sprünge deckt ein 5-ms-Splice-Duck pro Clip.
    Quantisierte Aktionen (Start phasenstarr / Retrigger Anker=Grid /
    Stop) als Pending-Action pro Track, sample-genau am Grid
    (`LaunchQuantization.h` = app-weites Enum, 4.5). Track-Mix: Gain-Slew,
    Balance-Pan (Mitte Unity), Mute/Solo (Scope pro Looper/global),
    Post-Fader-Meter. `LooperSessionModel` = MT-Slot-/Target-/Aktiv-
    Zustand über der Bank (EngineProcessor-frei, module-ready — einzige
    Lücke fürs LooperModule: BarSampleAnchors-Injektion). WICHTIG:
    prepareToPlay → `clearAllClips()` (Zombie-Pointer-Feld-Fund 05.07.).
  - **Persistenz:** `LooperSettings` (Conduit/Looper.settings, ValueTree↔
    XML): Struktur/Quellen/Mixer + alle Menü-Optionen (Quantisierung,
    Tap-Modus, ÷2-Hälfte, Reverse-Punkt, VARI-Raster Halbtöne|Session-
    Skala, VARI-Scope, Solo-Scope, sichtbare Slots, Delete-Latch,
    Auto-Advance). Clips selbst session-flüchtig (Save-Geste exportiert).
    Arming = VEREINIGUNG aller Looper-Quellen (Refcount-Diff — geteilte
    Quelle bleibt offen, bis der letzte Looper sie verlässt).
  - **UI:** Looper NEBENEINANDER (Mock-Layout); Fader OBEN mit vertikalem
    Wischen; VARI-Rotary in Oktaven (Detent 1×, DK-Reset, Rast-Button,
    Sync-Reset); TARGET-Kurzklick zykelt Tracks, Halten+Tap = Aktiv-
    Auswahl. Header-Kontext (nur Looper-Page): DELETE-/SAVE-HoldTiles
    (halten + Ziel antippen; Delete-Latch-Option) — Session-Save liegt
    im Browser (PROJEKTE → „Session speichern…").
  - **Clip-Thumbnail (09.07.2026):** der Commit schnappt die AKTUELLE
    Strip-Ansicht (Waveform ODER Spektrum) der committeten Takte —
    `LooperWaveformStrip::renderCommitThumbnail(startBeat, endBeat, w, h)`
    rendert „Tinte auf transparent" (Waveform = schwarze Min/Max-Spalten,
    Spektrum = Schwarz mit Intensitäts-Alpha; Pegel-Proxy = wahrgenommene
    Helligkeit der LUT-Pixel, beide Paletten sind monoton hell). Beat-
    Range = [commitEndBeat − contentBeats, commitEndBeat) des frischen
    Aktiv-Clips; SOFORT nach dem Commit rendern (History-/Spektrum-Ring
    halten nur ~16 Takte). Die LooperSlotCell zeigt es INVERTIERT zur
    Strip-Optik: Zellfläche = Quellfarbe (Master-Fallback LED-Grün),
    Tinte/Sweep/Aufbauten schwarz. Progress-Sweep = Fade-Schweif
    (09.07.2026): begrenztes Gradient-Band (~35 % Zellbreite) hinter der
    Abspielkante, läuft nach hinten transparent aus (Reverse gespiegelt) —
    NIE die ganze abgespielte Fläche flächig abdunkeln (verdeckt das
    Thumbnail). Kopfzeile OBEN (Dreieck + Label +
    Badges — die Waveform-Tinte ballt sich um die Zell-Mitte); Zell-Label
    der Thumbnail-Zellen = beim Commit eingefrorener Quell-Text der Combo
    („Live / wavetable"), klassische Zellen weiter „Clip N · X Bars".
    Der Schweif wickelt ZYKLISCH über die Loop-Grenze (alpha-stetig ans
    andere Zellende) — er verschwindet am Loop-Ende nie (User 09.07.2026).
    Kontrast-Regel: Aufbauten nehmen auf überwiegend schwarzen Stellen die
    Quellfarbe an (computeInkCoverage, Zonen-Deckung VORBERECHNET in
    setThumbnail — nie in paint messen). Lifecycle über clipId-Bindung:
    setThumbnail beim Commit [Editor], der 15-Hz-Timer cleart bei
    clipId-Mismatch oder leerem Slot (Delete, Überschreib-Commit,
    clearAllClips) — kein Zustand außerhalb der Zelle.
  - **Abspielposition monitor-synchron (09.07.2026):** der 15-Hz-Editor-
    Timer (refreshLooperStatus) liefert Struktur, Labels, Meter; die
    ABSPIELPOSITION (Zell-Sweep via `LooperSlotCell::setProgress`,
    Takt-Pie via setBarDisplay) und der Target-Puls laufen über einen
    eigenen VBlank-Pfad (`EngineEditor::tickLooperPlayheads`,
    juce::VBlankAttachment — UI-Regel „Animationen via VBlank"): pro
    Frame nur lock-freie Atomic-Reads (displayPhase01/stagedLengthBeats)
    der SPIELENDEN Slots, no-op wenn die Looper-Page nicht sichtbar ist.
    setProgress wirkt nur auf Zellen mit playing-State (Konsistenz mit
    dem Timer-Zustand); die Change-Guards von setProgress/setBarDisplay
    vergleichen EXAKT (juce::exactlyEqual) — Epsilons würden den Sweep
    wieder quantisieren (15-fps-Stufigkeit, User-Feedback 09.07.2026).
  - **OSC-Actions** (`/conduit/looper/…`, Indizes 1-basiert): stop |
    {n}/commit i:bars | {n}/stop | {n}/track/{t}/stop | {n}/target
    i:track i:slot — Muster /conduit/sync (Erkennung vor Endpoint-Lookup,
    AsyncUpdater → onLooperAction), fire-and-forget.
  - **Clip-Export** (Save-Geste): `LooperClipExporter` → CaptureWriter-Job
    (_l/_r, eingefrorene TrackSource, startPosition = commitStartSample →
    bext-align zu Capture-Exports; `CaptureService::enqueueExternalJob`).
