# Conduit Touch Lab

Eigenständiges Wegwerf-Harness, um **vor** der Conduit-Integration zu *messen*,
ob rohe Touch-Daten (Windows' Vorverarbeitung umgehend) auf deinen Monitoren
real besser sind als der native Pfad — und wie stark eine Filterkette
(Dead-Zone=0, One-Euro, Jitter-Gate) die langsamen, bewussten Fahrten glättet.

Das Projekt ist **komplett getrennt von Conduit**: eigener JUCE-Download,
eigener Build, keine Berührung des Conduit-Builds.

---

## 1. Einrichten (einmalig)

1. Diesen Ordner `ConduitTouchLab/` nach `C:\conduit Touch Lab\` **kopieren**
   (aus dem Build-Pfad möglichst Leerzeichen heraushalten — z. B.
   `C:\ConduitTouchLab\` ist am robustesten).
2. Visual Studio 2026 + CMake installiert vorausgesetzt (wie bei Conduit).

## 2. Bauen & Starten

In einer Eingabeaufforderung im kopierten Ordner:

```
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
```

Die App liegt danach unter
`build\TouchLab_artefacts\Debug\Conduit Touch Lab.exe` — auf dem
**Touch-Monitor** starten.

Beim ersten Configure lädt CMake JUCE (8.0.4) herunter — das dauert einmalig
etwas.

## 3. Was du siehst

- **Große Fläche links:** Deine Fahrten. Cyan = Nativ (roh), Orange =
  Raw-Pointer (roh), **weiße Linie** = Nativ gefiltert, **grüne Linie** =
  Raw-Pointer gefiltert. Beide Quellen zeichnen gleichzeitig übereinander —
  das *ist* der Vergleich.
- **Regler rechts:** jede Filterstufe einzeln an/aus + Parameter, live.
- **Messwerte unten:** je Spur Report-Rate (Hz), Jitter im Stillstand (σ px)
  und Geradheit langsamer Striche (RMS px).

## 4. Messprotokoll

1. **Stillstand-Jitter:** Finger ruhig auflegen, „Jitter σ px" Nativ vs.
   Raw-Pointer ablesen. Kleiner = ruhiger.
2. **Report-Rate:** langsam fahren, „Rate Hz" je Quelle vergleichen. Höher =
   mehr Punkte = potenziell feiner.
3. **Geradheit:** langsamen geraden Strich ziehen, „Gerade RMS px" vergleichen
   — einmal mit „One-Euro an", einmal aus. Kleiner = gerader.
4. **Blindvergleich:** „Blindvergleich" anschalten. Ein Punkt folgt dem
   verdeckt zugeordneten Kanal A/B — mit „Kanal A/B" umschalten, fühlen,
   „A feiner"/„B feiner" tippen. Nach ≥10 Runden „Auflösen" → Auszählung
   Nativ vs. Raw-Pointer.

## 5. Falls die Raw-Pointer-Spur leer bleibt

Windows liefert WM_POINTER **oder** WM_TOUCH, nie beides. Registriert JUCE das
Fenster für WM_TOUCH, kommt beim Raw-Arm nichts an (Rate Hz = 0 für „Raw-Ptr").
Dann **„WM_TOUCH abmelden (falls Raw leer)"** anhaken — der Pointer-Stack
stellt danach wieder zu. Einzelfinger-Nativ läuft über Maus-Promotion weiter,
also genau dein Fader-Fahr-Fall.

## 6. Entscheidungsregel (danach)

- Raw-Pointer messbar besser **und** gewinnt Blind-A/B → das Raw-Pointer-
  Backend lohnt die Portierung nach Conduit.
- Nativ ≈ Raw-Pointer → der Feind ist die Monitor-Firmware oder eine
  verschluckende Totzone; dann ist **allein die Filterkette** (Dead-Zone=0 +
  One-Euro) der Conduit-Liefergegenstand, kein Backend-Umbau nötig.

## 7. Was später verbatim nach Conduit wandert

- `Source/TouchFilterChain.h` — portabel, allocation-frei, header-only.
- `Source/TraceView.*` + `Source/MetricsPanel.*` — Prototyp des Diagnose-Felds
  für einen neuen „Touch"-Tab im Settings-Fenster.

Alles andere (Fenster, Regler-Form, Raw-Pointer-Subclass) ist Harness-Gerüst.
