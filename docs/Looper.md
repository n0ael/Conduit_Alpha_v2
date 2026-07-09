# Looper (Retro-Looper + Vollausbau) — Subsystem-Dossier
> Ausgelagert aus CLAUDE.md v4.6 §10.0, Juli 2026. Für Arbeiten an
> diesem Subsystem verbindlich wie die CLAUDE.md selbst (§1.1).

- **Retro-Looper** (`Source/Core/Looper` + `Source/UI/LooperPage`, Stand
  07/2026 — Endlesss-Muster auf Capture-Audio-Basis, MVP = ein Loop):
  - Immer aufnehmend: Quelle = Capture-Kanal („master" = Master-Output-Tap
    master_l/_r nach dem GraphFader | „hw:{paar}" = Eingangs-Paar |
    „out:{paar}" = Ausgangs-Paar hinter dem Master (Kanäle 2p/2p+1, Taps
    out{p}_l/_r, in prepareToPlay an die Device-Kanalzahl angeglichen —
    seit 08.07.2026, damit z. B. Link-Receive-Routings loopbar sind) |
    „tap:{name}" — Link-Receive-Module registrieren ihre Ausgabe seit
    08.07.2026 selbst als Capture-Kanäle {moduleId}_l/_r und sind damit
    direkt wählbar), Arming (`CaptureService::setChannelArmed`) hält das
    Gate zwangsweise offen. Wellenform/Spektrum tragen die Farbe der
    Quelle (Kanal-Farbe aus ChannelNames bzw. nodeColour;
    `LooperWaveformStrip::setSourceColour` tönt auch die Spektrum-LUT).
    Quelle + Ausgabe-Paar persistiert in TransportSettings
    (looperSource/looperAnchor).
  - **Quellen-Combo (09.07.2026):** Link-Receive-Taps zeigen
    „{targetPeer} / {targetChannel}" (Format des Receive-Panels; ohne
    Kanal-Wunsch „Link: {moduleId}") und bilden eine per Separator
    getrennte Gruppe HINTER den lokalen Quellen — ein Abschnitt pro Peer
    (App/Programm), Doppel-Label disambiguiert der Modulname. Alle
    Einträge und der Combo-Text der Auswahl tragen die Quellfarbe
    (PopupMenu-Item-Farbe via `getRootMenu()`; Auswahl über Item-IDs =
    Quell-Index + 1, NIE über Item-Indizes — Separatoren verschieben sie).
    Live-Refresh: zusätzlich zu CaptureService/ChannelNames-Broadcasts
    lauscht der EngineEditor als Root-Tree-Listener auf targetPeer/
    targetChannel/nodeColour/numInput-/numOutputChannels.
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
    setThumbnail beim Commit [Editor], der 30-Hz-Timer cleart bei
    clipId-Mismatch oder leerem Slot (Delete, Überschreib-Commit,
    clearAllClips) — kein Zustand außerhalb der Zelle.
  - **OSC-Actions** (`/conduit/looper/…`, Indizes 1-basiert): stop |
    {n}/commit i:bars | {n}/stop | {n}/track/{t}/stop | {n}/target
    i:track i:slot — Muster /conduit/sync (Erkennung vor Endpoint-Lookup,
    AsyncUpdater → onLooperAction), fire-and-forget.
  - **Clip-Export** (Save-Geste): `LooperClipExporter` → CaptureWriter-Job
    (_l/_r, eingefrorene TrackSource, startPosition = commitStartSample →
    bext-align zu Capture-Exports; `CaptureService::enqueueExternalJob`).
