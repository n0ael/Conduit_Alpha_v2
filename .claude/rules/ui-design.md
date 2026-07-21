---
paths:
  - "Source/UI/**"
  - "Source/Core/EngineEditor.*"
---

# Rule: ui-design — Push-3-Design-System & Touch-Regeln (CLAUDE.md 10)

- **PushLookAndFeel** (`Source/UI/PushLookAndFeel`) ist Default-LookAndFeel
  der App (gesetzt im EngineEditor): Jost als App-Font (BinaryData,
  `Assets/Fonts/`, OFL), dunkle Kacheln (#262626 auf #121212/#1a1a1a),
  LED-Akzente (grün Play, rot Automate/Looper, cyan Link, orange Capture).
- **PushIcons** (`Source/UI/PushIcons`): ALLE Symbole als `juce::Path` aus
  einem normierten 0..1-Quadrat in die Ziel-Bounds skaliert — vektorbasiert,
  DPI-fähig, keine Bitmaps. Bausteine: IconTile/TextTile/ValueTile
  (`PushTiles`).
- **Pages** (`Source/UI/PageHost`): Grid (Ω) · Mixer (∥∥) · TouchLive
  (Mini-Kanalzüge, Slot 2 — Rule `touchlive`) · Device (|||, Patch-Canvas).
  Device, Grid (M1) und TouchLive (M1c) implementiert — Mixer bleibt
  gestylter Platzhalter; die Clip-Page (▷▭) bekommt später wieder einen
  eigenen Slot (09.07.2026).
- **Schrift wird NIE horizontal gestaucht (User-Regel 07/2026):** bei
  Platzmangel Schriftgröße reduzieren oder Text kürzen — niemals quetschen.
  `drawFittedText`/`drawLabel` immer mit minimumHorizontalScale = 1.0
  (PushLookAndFeel::drawLabel erzwingt das app-weit);
  `Label::setMinimumHorizontalScale (< 1.0f)` ist verboten.
- Nicht-ASCII-Literale in `juce::String` IMMER über `String::fromUTF8`
  (CharPointer-Assertion).
- Touch-first: `setAcceptsTouchEvents(true)`, minimale Touch-Target-Größe
  44px, vollständig Mouse/Keyboard-kompatibel — kein Touch-only Code.
- **Gesten-Parität (User-Regel 18.07.2026):** Jede Geste existiert in
  drei Pfaden: Touch nativ, Trackpad (2-Finger nativ via Magnify/Scroll;
  höhere Ebenen via Modifier-Taste), Maus+Tastatur. Kein Feature ist
  touch-only, keines touch-verkrüppelt.
- **Eingaberegeln sind seitenspezifisch (User-Regel 18.07.2026):** Jede
  Page definiert ihre eigene Eingabe-Tabelle (Touch, Trackpad, Maus+Tasten)
  in ihrer Rule bzw. ihrem Dossier — Grid: Rule `grid` (MPE-Flächen
  verhalten sich selbst wie ein Trackpad), TouchLive/EQ8: docs/TouchLive.md,
  Node-Patch-Editor: Rule `node-editor` + docs/NodeEditor.md (umgesetzt
  18.07.2026). Die Gesten-Tabelle unten gilt nur, wo eine Page nichts
  Eigenes definiert.
- Jedes UI-Element mit Touch-State reagiert in ≤ 1 Frame visuell; keine
  blockierenden Operationen im `paint()`-Callback.
- **UI-Framerate (User-Regel 14.07.2026, ersetzt die alte 30-fps-Regel):**
  Anzeige-Refreshes (Meter, Scopes, Playheads, Modulations-Marker) laufen
  NATIV mit der Monitor-Rate über `UiFramePacer`
  (`Source/UI/UiFramePacer.h` = VBlankAttachment + globales Limit aus
  `UiSettings::uiFpsLimit`: Default 120 „Nativ", Drossel-Modi 60/30 im
  Oberfläche-Tab/Dev-Panel). KEINE festen `startTimerHz`-Refreshes mehr;
  dt-basierte Physik-Ticks (Federn/Gravity) bleiben direkte
  `VBlankAttachment`s; niederfrequentes Status-Polling (LED-Badges ≤ 10 Hz,
  seltene Wechsel) bleibt als Timer erlaubt. Audio schreibt weiterhin
  lock-free Ringbuffer, die UI liest pro Frame-Tick.
- UI-Components binden NUR an den ValueTree-Subtree, nie an den Processor
  (Zombie-UI-Schutz, CLAUDE.md §5 / docs/PatchEngine.md 5.3);
  `stopUpdates()`-Hook für Phase 1. Sanktionierter Laufzeit-Zugriff auf
  Modul-Objekte ausschließlich über die NodeUiRegistry (ADR 014).
- Stille Lebensdauer-Kontrakte: Service-Pointer in UI (LevelMeter, Taps,
  ChannelNames, UiSettings …) sind EngineProcessor-Member und überleben
  jede UI; GraphManager-Service-Pointer folgen der Deklarationsreihenfolge
  im EngineProcessor.

Touch-Gesten (App-weiter FALLBACK — gilt nur, wo eine Page nichts
Eigenes definiert, CLAUDE.md §10.0; Node-Patch-Editor → Rule
`node-editor`/docs/NodeEditor.md, Grid → Rule `grid`, TouchLive →
docs/TouchLive.md):

| Geste | Funktion | Priorität |
|---|---|---|
| 1 Finger Drag | Parameter-Sweep (CV-Wert) | P0 |
| 2 Finger Pinch | Range-Zoom Scope/Visualizer | P0 |
| Grid: 1 Finger (Sonne) | Note + Pitch-Bend (X) + Ausdruck (Y) | P0 |
| Grid: 2. Finger im Orbit (Mond) | Ring — Radius → Slide, keine neue Note | P0 |
| 2 Finger Rotate | LFO-Phase / Tuning | P1 |
| Long Press | Kontextmenü / Node-Eigenschaften | P2 |

(Der frühere Eintrag „3 Finger Tap = Snap-to-Zero/Reset" war ein
Phantom — app-weit nie implementiert (M0-Inventur 18.07.2026); die
3-Finger-Ebene ist seitenspezifisch belegt, z. B. Birdeye im
Node-Patch-Editor.)
