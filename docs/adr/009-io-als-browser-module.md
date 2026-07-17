# ADR 009 — I/O als reguläre Browser-Module (§6.2-Regel entfällt)

Status: Beschlossen 17.07.2026 · Betrifft: §6.2, GraphManager,
Delete-Pfad, I/O-Konsolidierung (Roadmap 08.07.2026)

## Entscheidung
Die reservierten moduleIds `audio_input`/`audio_output` entfallen.
Audio-I/O wird durch reguläre Module aus dem Browser abgebildet, die
intern als Proxy auf die Graph-I/O-Prozessoren (AudioGraphIOProcessor)
des EngineProcessor verbinden. Factory-Materialisierung,
createState(), voller Delete-Pfad — keine Sonderbehandlung im
GraphManager.

## Begründung
- Mehrfach-Instanzen: lokale Output-Module pro Seite statt
  Cross-Page-Kabel zum einen Master-Out (der Graph summiert mehrere
  Connections auf denselben Pin nativ). Reduziert den
  Portal-Badge-Bedarf (ADR 008 M5) erheblich.
- I/O-Konsolidierung ("startet stereo, + fügt Hardware- ODER
  Link-Kanäle hinzu") ist als reguläres Modul mit createState()
  sauberer als als Factory-Ausnahme.
- GraphManager verliert den Reserved-Id-Zweig, Delete-Pfad verliert
  die Ausnahme.

## Konsequenzen & Auflagen
- Patch ohne Output = Stille: Default-Patch (neu + Migration) enthält
  ein Stereo-Out; löschbar, aber vorhanden.
- Delete eines I/O-Moduls löscht seine Connections mit — UndoManager
  fasst Modul + Connections als EINE Transaktion; Catch2-Test Pflicht
  (Delete + Undo stellt beides wieder her).
- Delete-Armierung: Doppel-Tap global; Output-Module mit Connections
  zeigen inline Warnzustand (Details ADR 008).
- Migration: Reserved-Id-Subtrees werden in reguläre Modul-Subtrees
  gewandelt; gemeinsamer stateVersion-Bump mit ADR 008 (M1).
- Die Graph-I/O-Prozessoren des EngineProcessor existieren
  unverändert weiter; nur die sichtbare Modul-Schicht ändert sich.
