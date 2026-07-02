# ConduitLFO — Max4Live-Testdevice für das Announce-Protokoll

> Referenz-Implementierung für CLAUDE.md 7.4. Das Device macht **kein Audio** —
> der LFO läuft nativ (audio-rate, beat-synchron) in Conduit; das Device ist
> reine Fernbedienung plus Visitenkarte.

## Was es tut

1. **Announce:** Sobald die Live API bereit ist (`live.thisdevice`, nicht
   `loadbang`), sendet `conduit_announce.js`
   `/conduit/announce <remoteId> lfo <Track-Name> <Track-Farbe>` an
   Conduit (UDP :9000) — Conduit legt automatisch die gespiegelte
   LFO-Kachel an, benannt und getönt nach dem Live-Track.
2. **Persistente remoteId:** Beim ersten Start würfelt das js eine ID und
   legt sie in der versteckten `live.numbox` „RemoteSeed" ab (Parameter
   *Stored Only* → wird mit dem Live-Set gespeichert). Nach Neustarts
   beider Seiten finden sich Device und Conduit-Node darüber wieder.
3. **Re-Announce-Heartbeat (30 s):** Startet Conduit später oder wurde die
   Kachel gelöscht, erscheint sie beim nächsten Heartbeat wieder.
4. **Dials:** Rate (0.0625–4 Zyklen/Beat) und Depth (0–1) senden über die
   rename-feste Alias-Adresse `/conduit/remote/<remoteId>/rate|depth` —
   ein Umbenennen der Kachel in Conduit ist dem Device egal.

## Installation

1. Ordner `ConduitLFO` irgendwo ablegen (`.maxpat` + `conduit_announce.js`
   müssen zusammenbleiben).
2. In Live: das `.maxpat` in ein leeres **Max Audio Effect** ziehen oder das
   Patch als `.amxd` speichern (Max: *File → Save As…*, Format „Max for
   Live Device").
3. Device auf den gewünschten Track legen — die Kachel erscheint in Conduit
   mit Track-Name und -Farbe.

## Conduit auf einem anderen Rechner

Standardziel ist `127.0.0.1:9000`. Für einen anderen Host die Message-Box
`host <IP>` im Patch anpassen und anklicken (konfiguriert `udpsend` live).

## Feedback-Richtung (optional)

Das Minimal-Device hat bewusst **keinen** `udpreceive` — Parameter-Feedback
aus Conduit (7.3, Send-Pfad :9001) initialisiert nur reichere Clients
(z.B. TouchOSC). Wer die Dials aus Conduit nachführen will: `udpreceive 9001`
+ `route /conduit/remote/<id>/rate ...` ergänzen und die Dials mit
`set $1` speisen (Echo-Schleife vermeiden).
