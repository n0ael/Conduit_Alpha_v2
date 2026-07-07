# Max4Live-Announce (Remote-Module) — Subsystem-Dossier
> Ausgelagert aus CLAUDE.md v4.6 §7.4, Juli 2026. Für Arbeiten an
> diesem Subsystem verbindlich wie die CLAUDE.md selbst (§1.1).

- **Format:** `/conduit/announce s:remoteId s:factoryKey s:trackName
  i:trackColour` (Farbe 0x00RRGGBB aus der Live API; Float32 wird toleriert
  — Max/js garantiert die Int-Kodierung nicht).
- **remoteId — dokumentierte Ausnahme zur Regel 6:** die ID ist in BEIDEN
  Welten persistent (Live-Set speichert sie als Device-Parameter „Stored
  Only", der Conduit-Patch als Node-Property `remoteId`) — keine
  Laufzeit-ID. Hartes Format statt Sanitizing: `[A-Za-z0-9_-]`, max. 64
  (wird Teil von OSC-Adressen; eine umgeschriebene ID fände ihr Live-
  Gegenstück nie wieder).
- **Verarbeitung:** Netzwerk-Thread validiert und sammelt (`pendingAnnounces`,
  eigener Lock) + `triggerAsyncUpdate` → Message Thread: `onAnnounce` →
  `RemoteModuleBinder::handleAnnounce()` (find-or-create). Existiert →
  idempotent (nur `tintColour` nachziehen; Name/Position sind nach der
  Erst-Anlage User-Hoheit). Neu → `ModuleFactory::isRegistered`-Whitelist,
  `addModuleNode(factoryKey, pos, configure)` (configure setzt
  remoteId+Tint VOR dem Einhängen), dann `renameNode` auf den Track-Namen
  (Kollision → Auto-Name bleibt).
- **Alias-Adressen (receive-only):** `rebuildEndpoints()` registriert für
  remoteId-Nodes ZUSÄTZLICH `/conduit/remote/{remoteId}/{paramId}` auf
  denselben Endpoint — das Device adressiert nur über seine remoteId,
  User-Renames und Kollisions-Suffixe sind ihm egal. Der Send-Pfad bleibt
  kanonisch (`/conduit/{type}/{moduleId}/{paramId}`, Helper `OscAddress.h`).
- **Kein Auth** (LAN-Annahme wie der übrige Empfang) — Whitelist +
  Zeichen-Limits + Idempotenz decken Garbage ab. Node in Conduit gelöscht,
  Device lebt → der 30-s-Re-Announce legt neu an (gleiche remoteId).
- **UI:** `NodeComponent` zeigt `tintColour` als Streifen unter der
  Kopfzeile, folgt Re-Announces live. Referenz-Device:
  `Tools/Max/ConduitLFO/` (kein Audio im Device — der LFO läuft nativ).
