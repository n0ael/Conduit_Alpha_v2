---
paths:
  - "Source/UI/**"
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
- Jedes UI-Element mit Touch-State reagiert in ≤ 1 Frame visuell; keine
  blockierenden Operationen im `paint()`-Callback; Animationen via
  `juce::VBlankAttachment`; Meter/Scope-Refresh 30 fps (Audio schreibt
  lock-free Ringbuffer, UI liest).
- UI-Components binden NUR an den ValueTree-Subtree, nie an den Processor
  (Zombie-UI-Schutz, CLAUDE.md 5.3); `stopUpdates()`-Hook für Phase 1.

Touch-Gesten (App-weit):

| Geste | Funktion | Priorität |
|---|---|---|
| 1 Finger Drag | Parameter-Sweep (CV-Wert) | P0 |
| 2 Finger Pinch | Range-Zoom Scope/Visualizer | P0 |
| Grid: 1 Finger (Sonne) | Note + Pitch-Bend (X) + Ausdruck (Y) | P0 |
| Grid: 2. Finger im Orbit (Mond) | Ring — Radius → Slide, keine neue Note | P0 |
| 2 Finger Rotate | LFO-Phase / Tuning | P1 |
| 3 Finger Tap | Snap-to-Zero / Reset | P1 |
| Long Press | Kontextmenü / Node-Eigenschaften | P2 |
