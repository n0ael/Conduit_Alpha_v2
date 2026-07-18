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
  gewandelt; eigener stateVersion-Bump in M2 (Sequenz-Korrektur
  18.07.2026, siehe ADR 008 Datenmodell — die Wandlung setzt die in
  M2 entstehenden Modul-Typen voraus).
- Die Graph-I/O-Prozessoren des EngineProcessor existieren
  unverändert weiter; nur die sichtbare Modul-Schicht ändert sich.

## Umsetzungsnotizen (M2, 18.07.2026)
- `AudioEndpointModule` (Pass-Through, leerer processBlock) mit den
  BISHERIGEN factoryIds `audio_input`/`audio_output` — dadurch bleiben
  Bestandspatches und alle UI-Sonderausstattungen (Meter,
  ChannelNames-Labels, Pairing, Send-Toggles, Kabel-Quellfarben,
  Looper-Quellen) unverändert funktional.
- Anker-Kabel: `IExternalAudioEndpoint`-Mixin; der GraphManager
  verbindet Proxys bei der Materialisierung implizit mit den
  registrierten AudioGraphIOProcessor-Ankern (kein Patch-Zustand,
  nicht im Tree; syncConnections gleicht nur Kabel zwischen
  tree-verwalteten Nodes ab und rührt sie deshalb nicht an).
- Tree-Schema unverändert: Port-Sicht 0/N bzw. N/0; die Graph-Busse
  des Proxys sind N/N. Kanalzahl-Änderung (Gerätewechsel)
  re-materialisiert das Modul (hartes removeNode — der Gerätewechsel
  startet Audio ohnehin neu).
- `ensureIONodeStates` wurde zu `migrateReservedIO`
  (rootStateVersion 3): Default-I/O nur noch bei Patches < V3 —
  ab V3 kein Auto-Repair, gelöschte I/O-Module bleiben gelöscht.
- Kanalzahl folgt der Hardware für ALLE Instanzen
  (syncHardwareIOChannels-Schleife); neue Browser-Instanzen bekommen
  sie sofort (childAdded-Hook, lastDevice-Merker). Die
  I/O-Konsolidierung („startet stereo, + fügt Kanäle hinzu") bleibt
  ein späteres, separates Feature.
- Delete: der reguläre zweiphasige Pfad löscht Modul + Kabel bereits
  als EINE Transaktion (processPendingDeletes) — Auflage erfüllt.
