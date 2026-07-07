# OSC-Send (Parameter-Feedback an Clients) — Subsystem-Dossier
> Ausgelagert aus CLAUDE.md v4.6 §7.3, Juli 2026. Für Arbeiten an
> diesem Subsystem verbindlich wie die CLAUDE.md selbst (§1.1).

- **Snapshot-Diff statt Listener:** ein `paramValue`-Listener kann
  OSC-Empfang, UI, Undo und Preset-Load nicht unterscheiden. Der
  `OscSendService` läuft deshalb als `juce::Timer` @ 30 Hz auf dem Message
  Thread: Tree-Walk über Nodes[], Diff gegen den `lastSent`-Cache mit Key
  **`(nodeUuid, paramId)`** (nicht Adresse — rename-sicher). Deleting-Nodes
  werden wie in der Receive-Registry übersprungen, Cache-Einträge
  verschwundener Nodes gepruned. Der Audio Thread ist NIE beteiligt (3.1).
- **Echo-Suppression:** `OscController::applyTreeUpdates()` meldet jeden in
  den Tree übernommenen Empfangswert via `onRemoteValueApplied` →
  `OscSendService::noteRemoteValue()` impft den Cache VOR dem nächsten
  Tick — der eigene Empfang wird nie zurückgesendet. UI/Undo/Preset-Load-
  Writes diffen dagegen und gehen raus (gewollt).
- **Float-Diff-Falle:** `var` speichert double, der Cache float — beidseitig
  über `float` vergleichen (`juce::exactlyEqual`), sonst Dauersende-Schleife.
- **Transport:** ein `OSCBundle` pro Tick, Chunking bei >50 Messages
  (UDP-Paketgrenze); `IOscSink`-Seam für Tests, `juce::OSCSender` in der App.
- **`OscSendSettings`** (App-Zustand, Muster `MeterSettings`): Host / Port /
  Enabled in `Conduit/OscSend.settings`. **Default-Port 9001, NICHT 9000**
  (Loopback-Schutz gegen den eigenen Empfang), Enabled default aus.
  Aktivierung leert den Cache → erster Tick ist ein impliziter Voll-Sync.
- **`/conduit/sync`:** Client fordert den Voll-Dump an — Erkennung VOR dem
  Endpoint-Lookup [Netzwerk-Thread], Ausführung via atomic Flag +
  AsyncUpdater auf dem Message Thread (`sendFullDump()`).
- **IP-Learn (Learn-Probe):** `juce::OSCReceiver` verwirft die Absender-IP
  (`socket->read(...)` ohne senderAddress, `OSCInputStream` nicht
  wiederverwendbar) — deshalb `OscController::beginIpLearn()`: Receiver
  kurz trennen, eigener `DatagramSocket` bindet den Empfangsport
  (Bind-Retry gegen das Rebind-Fenster, v.a. Windows), `read()` liefert die
  IP des ersten UDP-Pakets, Receiver wird restauriert (auch bei
  Timeout/Cancel/Destruktor). Kein OSC-Parsing nötig.
