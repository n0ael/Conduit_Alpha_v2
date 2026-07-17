# ADR 008 — Node-Page Multipage: Seiten, Gesten-Leiter, Performance-Modus

Status: Beschlossen 18.07.2026 · Betrifft: Datenmodell (§6.2), UI
(§10), Plattform-Setup (§9)

## Entscheidung
Mehrere unendliche Canvas-Seiten auf der Node-Page. Seiten sind eine
reine View-Schicht über EINEM `AudioProcessorGraph` — kein Graph pro
Seite. Der Audio-Thread kennt keine Seiten.

## Geltungsbereich & Leerraum-Regel
- Die Gesten-Leiter dieses ADRs gilt AUSSCHLIESSLICH im
  Node-Patch-Editor. Andere Pages (EQ8, MPE-Grid u. a.) definieren
  eigene Eingaberegeln in ihren Dossiers/Rules (§10.0).
- **Leerraum-Regel:** Canvas-Gesten werden NUR erkannt, wenn KEINER
  der beteiligten Touchpoints auf einem Modul BEGINNT. Touches, die
  auf einer Modul-Component landen, gehören dem Modul
  (JUCE-Hit-Testing); der Canvas-Gesten-Recognizer zählt
  ausschließlich Touches, die auf dem Canvas-Hintergrund beginnen.
  Beispiel: 2-Finger-Pinch mit einem Finger auf einem Modul ist KEIN
  Zoom. Modul-Interaktionen (Drag, Parameter, Doppel-Tap-Delete)
  sind dadurch per Konstruktion kollisionsfrei zur Gesten-Leiter.

## Datenmodell
- Neuer RootTree-Zweig `Pages`: je Seite `pageUuid` (persistent,
  Referenzanker), `gridX`/`gridY` (Koordinaten, mutierbar), optional
  `name`, Viewport (`viewOffsetX`, `viewOffsetY`, `viewZoom`).
- Jeder Node erhält Property `pageUuid`. Seitenwechsel eines Nodes ist
  ein `setProperty` (ein Undo-Step), NIE removeChild/addChild — sonst
  feuern Delete-Pfad und OSC-Deregistrierung fälschlich.
- Connections bleiben unverändert (referenzieren Node-Uuids;
  Seitenzugehörigkeit ist reine Render-Frage).
- Seiten-Löschen nur wenn leer (Regel a). Kein Bulk-Delete-Pfad.
- Migration: stateVersion-Bump am RootTree; Bestandspatches erhalten
  eine Seite (0,0), alle Nodes deren pageUuid. Migrations-Sequenz
  korrigiert 18.07.2026: M1 bumpt für Pages, M2 bumpt separat für
  die I/O-Wandlung — ein gemeinsamer Bump würde zwischen M1 und M2
  Subtrees erzeugen, die auf noch nicht existierende Modul-Typen
  zeigen.

## Gesten-Leiter (Fingerzahl = Ebene; vorbehaltlich Inventur M0)
| Finger | Aktion | Ebene |
|---|---|---|
| 2 Pinch | Zoom kontinuierlich | Canvas |
| 2 Drag | XY-Pan | Canvas |
| 3 | Birdeye der AKTIVEN Seite | Seite |
| 4 Swipe | Wechsel zur Nachbarseite (Peek/Commit) | zwischen Seiten |
| 5 | Seiten-Selektion: alle Seiten als Kacheln | Patch gesamt |

- Arbeits-Zoom und Birdeye-Zoom sind als PEGEL im Menü einstellbar —
  keine Verhaltens-Schalter (Quasimode-Prinzip: Gesten bleiben
  deterministisch, nur Stufen sind justierbar).
- 3-Finger-Birdeye: HOLD (transient — Finger halten = Übersicht,
  Karte bewegt sich unter fixem Mittel-Target, Loslassen = Zoom auf
  Viewport der Zielseite). Entschieden per M0-Inventur 18.07.2026:
  auf der Node-Page existiert kein 3-Finger-Code; der in Rule
  `ui-design` dokumentierte "globale 3-Finger-Tap" hat app-weit
  KEINE Implementierung (Phantom-Eintrag; Rule-Bereinigung =
  separater späterer Mini-Auftrag). Die lokalen 3-Finger-Gesten
  anderer Pages (MpeShapingView 2s-Hold, TouchLiveEq8 Drag) sind
  durch die Seitenspezifik (§10.0) unberührt.
- 4-Finger-Swipe: Nachbarseite peekt live mit, Schwelle entscheidet
  Commit/Snap-back; Wisch ins Leere legt eine neue Seite an.
- 5-Finger-Selektion: Zoom-out-Animation auf Kachel-Grid der
  Seiten-Koordinaten, leere Seiten GEDIMMT (nicht ausgeblendet),
  Tap auf Kachel = Zoom-in auf deren gespeicherten Viewport
  (Fallback: Arbeits-Zoom).
- Doppel-Tap auf Modul = Delete-Armierung (global, alle Module,
  gesamte Kachelfläche inkl. Titel-Label); Output-Module mit >=1
  Connection zeigen inline non-modalen Warnzustand
  (JUCE_MODAL_LOOPS_PERMITTED=0 — kein blockierender Dialog),
  Timeout ~3 s. Kollisionsauflösung (M0-Befund 18.07.2026): das
  bestehende editOnDoubleClick des Titel-Labels
  (NodeComponent.cpp, titleLabel.setEditable) wird in M3b/M4
  ENTFERNT — Rename läuft ausschließlich über das bestehende
  NodeAttributePanel (Long-Press Farbpunkt); keine Zone innerhalb
  einer Kachel, in der dieselbe Geste anderes tut. Der bestehende
  Canvas-Doppel-Tap (Attenuator-Provisorium "bis zur Modul-Palette")
  bleibt vorerst und kollidiert nicht.
- Übersichts-Rendering: gecachte Proxy-Miniaturen pro Seite
  (juce::Image), Invalidierung via ValueTree-Listener, Neuaufbau
  VBlank-gesteuert max. eine Miniatur pro Frame, NIE in paint().
- Tuning-Parameter (Peek-Dauer, Dwell, Schwellen, Animationszeiten)
  im Dev-Menü; nach Erprobung fixiert und entfernt.

## Gesten-Parität (Trackpad/Maus-Mapping)
- Trackpad Ebene 2 NATIV: macOS Magnify-Events (Pinch) +
  Scroll-Events (Pan); Windows Precision Touchpad analog.
- Ebenen 3/4/5 auf Trackpad: gehaltene Modifier-Taste schaltet die
  Ebene (Belegung in M0/M3 festlegen), eine Hand Trackpad, eine
  Tastatur.
- Maus: Rad+Modifier für Zoom, mittlere Taste/Modifier für Pan,
  Tastatur-Shortcuts für Seitenwechsel, Übersicht als Klickziel.

## Performance-Modus (Plattform-Setup, §9-Scope-Klarstellung)
- Fullscreen-borderless + je Plattform:
  - Windows: `System.EdgeGesture.DisableTouchWhenFullscreen`
    (SHGetPropertyStoreForWindow), Press-and-Hold-Rechtsklick aus
    (MICROSOFT_TABLETPENSERVICE_PROPERTY), Touch-Feedback-Visuals
    aus (SetWindowFeedbackSetting). 3/4-Finger-SYSTEMGESTEN sind
    nicht per App abschaltbar → dokumentierte Setup-Anforderung
    (Windows-Einstellungen), analog ASIO-Setup.
  - iOS: preferredScreenEdgesDeferringSystemGestures; 4/5-Finger-
    Multitasking-Gesten nur via Guided Access → Setup-Doku.
  - LinkBox (Linux Kiosk): Input gehört vollständig der App, kein
    Zusatzaufwand.
  - macOS Trackpad: OS konsumiert 3/4/5-Finger; Modifier-Pfad ist
    dort HAUPTWEG, kein Fallback.
- GestureHelper (Raw-Multitouch + Systemgesten-Umschaltung via
  separatem Prozess, Muster Push-Shuttle): eigener Roadmap-Spike,
  bewusst NICHT Teil dieses Features — Private-API-Risiko gehört
  nicht auf den kritischen Pfad.

## Seitenübergreifende Verbindungen (Variante β, Portal-Badge)
- Cross-Page-Connections sind normale Graph-Connections; nur das
  Rendering unterscheidet sich: Badge am Seitenrand Richtung
  Zielseite, Tap springt zur Gegenseite und highlightet das
  Gegenstück.
- Erzeugung: Kabel an den Seitenrand ziehen, Dwell (~500 ms,
  Dev-Tuning) wechselt die Ansicht, andocken auf der Zielseite.
- Badge-Aggregation: einzeln bis 3 Verbindungen pro Nachbarseite,
  ab 4 Sammel-Badge mit Zähler.
- Verworfen: Variante α (Send/Return-Modulpaar) — neue persistente
  Kopplungsart + neue Delete-Fehlerklassen für ein Rendering-Problem.
  Ein echtes Bus-/Summenmodul bleibt als unabhängiges
  UtilityModule-Feature möglich.

## Meilensteine
- M0: Read-only Gesten-/Canvas-Inventur (Belegung Node-Page,
  Doppel-Tap, 3-Finger-Tap/Hold-Koexistenz, Zoom-Implementierung,
  Viewport-State, Delete-Geste, Magnify-Event-Handling, AKTUELLES
  HIT-TESTING-ROUTING Modul vs. Canvas-Hintergrund). STOPP bei
  Konflikt.
- M1: Pages-Datenmodell + Migration (NUR Pages, eigener
  rootStateVersion-Bump; die ADR-009-I/O-Wandlung bumpt separat in
  M2 — Sequenz-Korrektur 18.07.2026, siehe Datenmodell) +
  Catch2-Tests. Keine UI.
- M2: I/O als Browser-Module (ADR 009).
- M3a: Canvas-Viewport-NEUBAU (M0-Befund: es existiert kein
  Zoom/Pan/Viewport/Transform, kein mouseMagnify/mouseWheelMove
  app-weit): Transform-Infrastruktur (Zoom+Offset), kontinuierlicher
  2-Finger-Pinch/Pan, unendlicher Canvas, mouseMagnify+Scroll-Pfad
  (Trackpad/Maus), zentraler wiederverwendbarer Gesten-Recognizer
  (Muster MouseInputSource::getIndex() der bestehenden Subsysteme —
  es existiert KEIN zentrales Gesture-Framework). Architektur-
  sensibel.
- M3b: Seiten-Navigation (4-Finger-Swipe Peek/Commit,
  Seitenerzeugung, Viewport-Persistenz-Anbindung, Regel a,
  Modifier-/Tastatur-Pfade) + Entfernung editOnDoubleClick
  Titel-Label.
- M4: Birdeye + Seiten-Selektion (Miniatur-Cache, 3-/5-Finger,
  Menü-Pegel, Dev-Tuning) + Performance-Modus-Grundlagen.
- M5: Portal-Badges. Bewusst zuletzt — Bedarf nach M2–M4 neu
  bewerten.
