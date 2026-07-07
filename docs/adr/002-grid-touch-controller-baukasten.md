### ADR: Grid-Page als Touch-Controller-Baukasten

Status: Akzeptiert — Juli 2026

Kontext:
Grid war als AbletonOSC-Remote-Page geführt (Conduit steuert Live fern).
Neue Produktabsicht: Grid wird der benutzerfreundlichste Touch-Controller-
Baukasten. Nutzer ohne Programmierkenntnisse bauen Controller-Layouts, die
interne (Conduit-Parameter/Makros) und externe Ziele steuern. Positionierung:
kein TouchOSC-Konkurrent, sondern mehr Gestaltungsfreiheit für Nicht-Programmierer.

Entscheidung:
1. Grid = Touch-Controller-Baukasten. AbletonOSC-Remote wird ein Ausgangs-Ziel
   innerhalb von Grid, keine eigene Page.
2. Architektur-Rückgrat, symmetrisch: mehrere Quellen -> ein internes
   Voice-/Control-Modell -> mehrere austauschbare Sinks, Quellen hinter einem
   Source-Interface und Sinks hinter einem Sink-Interface.
   - Quellen: Grid-Touch (u.a. MPE-Keyboard mit Circle-Mechanik),
     Hardware-MPE-Input (USB/DIN).
   - Sinks: (A) direkt MPE-MIDI (virtueller Port same-machine / Hardware-Port
     USB+DIN / macOS RTP-MIDI fuer Netz); (B) MPE-ueber-OSC (UDP -> externer
     Transcoder oder Max-Patch, remote/cross-platform); (C) CV (Software-CVC,
     per-Voice Pitch/Pressure/Slide auf DC-gekoppelte Outs, ES-9/MOTU).
3. Die MPE-Zuteilung (Finger -> Voice -> Kanal) liegt IN Conduit (testbar).
   Das OSC-Protokoll traegt bereits zugeteilte Voices, damit der externe
   Transcoder ohne musikalische Logik bleibt (Leos Max/JS-Domaene).
4. Same-machine nutzt den direkten MIDI-Sink; der OSC-Sink ist nur fuer Remote.
5. Circle-Mechanik: erster Finger = Note + Pitch-Bend ueber X + eine durchgehende
   Achse ueber Y; zweiter Finger spannt einen Kreis, dessen Radius eine zweite
   durchgehende Achse steuert. Y-Achse und Radius mappen auf die zwei
   durchgehenden MPE-Dimensionen (Channel Pressure / CC74-Slide), pro Element
   vertauschbar. Detail-Spezifikation im jeweiligen Meilenstein-Auftrag.

Konsequenzen:
+ Eine Engine, viele Ziele. Nicht-Programmierer erhalten Gestaltungsfreiheit.
+ Remote = OSC: netz-nativ, cross-platform, passt zur bestehenden OSC-Infra
  und zum Dedicated-Hardware-Ziel.
- MPE ist MIDI 1.0 (kein MIDI-2.0-Bedarf fuer dieses Feature).
- Direkter MIDI-Out-Sink ist neue Infrastruktur (§4.1 kennt bisher kein
  MIDI-Out-Modul). Virtueller Port ist plattformabhaengig.
- Die Circle-Gesten-Disambiguierung (Latch/Pinch/Drift) braucht eine eigene
  State-Machine und ist ein eigener Meilenstein.
- Windows-Kontext (Juli 2026): natives Network-MIDI noch Preview/Alpha ->
  der OSC-Weg ist die tragfaehige Remote-Loesung; nativer virtueller
  Port/Loopback ist inzwischen vorhanden.
