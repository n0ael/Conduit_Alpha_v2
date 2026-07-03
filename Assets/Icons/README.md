# Icon-Workflow (PushIcons ↔ SVG)

Die App rendert alle Symbole vektorbasiert aus `Source/UI/PushIcons.cpp`
(normiertes 0..1-Quadrat, CLAUDE.md 10.0). Dieser Ordner ist die
Austauschfläche für die Icon-Überarbeitung im Vektorprogramm:

- **`SVG alt/`** — Export des aktuellen Code-Stands (ein SVG pro Icon,
  viewBox 0 0 100 100, Dateiname = Icon-Name im `push::Icon`-Enum).
  Wird bei Code-Änderungen neu exportiert — hier NICHT editieren.
- **`SVG angepasst/`** — überarbeitete Versionen hier ablegen (gleicher
  Dateiname wie in `SVG alt/`). Claude pflegt sie dann zurück in
  `PushIcons.cpp` ein; danach wandert der neue Stand nach `SVG alt/`.

## Regeln fürs Bearbeiten

- **ViewBox 0 0 100 100 beibehalten**, Icon mittig mit ~10 % Rand.
- Alles in **Pfade konvertieren** (kein Text, keine Bitmaps, keine Filter).
- Zwei Gruppen im SVG (siehe Export):
  - Gruppe **stroke** = MITTELLINIEN — die Strichstärke kommt zur
    Laufzeit vom Code und skaliert mit der Kachelgröße; die Stärke im
    SVG ist nur Vorschau.
  - Gruppe **fill** = exakte Flächen — kommen 1:1 so an, wie gezeichnet.
    Wenn die genaue Dicke/Form wichtig ist: als Fläche anlegen
    („Kontur in Pfad umwandeln").
- Farben sind egal — die App färbt zur Laufzeit (LED-Zustände).
