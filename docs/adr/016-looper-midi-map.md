# ADR 016 — Looper-MIDI-Map: eigene Bindungs-Instanz, Ziel-Kodierung, MAP-Overlay

Status: Akzeptiert (20.07.2026) · Baut auf ADR 006 (MIDI-Rig) auf.

## Kontext

Der Looper soll wie in Ableton mappbar sein: MAP-Modus an, Element
antippen, Regler bewegen, fertig. Die Bindungs-Maschine dafür existiert
seit MIDI-Rig M5b (`grid::MidiInBindings` mit Learn, Soft-Takeover,
Glättung, Shift-Ebenen) — sie war aber an die Grid-Page gebunden und auf
`grid::MacroControlKey` gekeyt. Offen war: eine zweite Instanz oder eine
gemeinsame; wie Looper-Ziele adressiert werden; und wie ein Overlay über
eine Seite gelegt wird, die selbst voller Bedienelemente steckt.

## Entscheidungen

1. **Eigene `MidiInBindings`-Instanz** (`LooperMidiMap`) statt einer
   geteilten: Learn, Takeover, Smoothing und die Mappings-Liste kommen
   dadurch unverändert wieder zum Einsatz, während Grid- und
   Looper-Bindungen sich nie in die Quere kommen. Persistenz in einer
   EIGENEN Datei (`Conduit/LooperMidi.settings`, Muster LooperSettings) —
   die Grid-Bindungen liegen in der Grid-Session, beides bleibt getrennt
   sicherbar.
2. **Ziel-Kodierung als gepackter `controlId`** auf einer eigenen
   Layer-Ebene (`kLooperLayer = 2`; System/DIY sind Grid):
   `Typ << 12 | Looper << 8 | Track << 4 | Index`. Damit bleibt der
   Bindungs-Typ unverändert (`MacroControlKey` ist ein Int-Tripel), und
   die Kodierung ist pure, testbare Arithmetik
   (`Source/Core/Looper/LooperMidiTargets.h`). Anzeigenamen zählen Tracks
   GLOBAL im 4er-Raster — dieselbe Sprache wie Patch-OUT und Strips.
3. **Klassisches Learn** (User-Entscheidung gegen die Handoff-Variante
   „nächste freie CC ab 20"): Ziel antippen, dann den Controller bewegen;
   dessen Adresse wird gebunden. Das passt zu bestehenden Setups und
   erlaubt beliebige CC/Kanäle statt eines festen Rasters.
4. **Eingänge von ALLEN Controller-Rolle-Geräten** des Rigs (nicht nur
   dem Grid-Controller), verdrahtet über denselben 60-Hz-Message-Thread-
   Drain des `MidiPortHub`. Der Audio-Thread bleibt unbeteiligt (ADR 006
   E7). Rig-Änderungen verdrahten die Abos neu.
5. **Dispatch über die vorhandenen UI-Hooks**, nicht über neue Pfade:
   kontinuierliche Ziele (Gain, Pan, Distanz, Send-Level, LEN/POS, VARI)
   schreiben in die LooperSettings bzw. rufen die Row-Hooks; Schalt-Ziele
   (Slots, Segmente, Play/Stop, M/S, Reverse, MST, Stop All) feuern auf
   der steigenden Flanke. Es gibt damit KEINE zweite Wahrheit neben der
   Maus-Bedienung.
6. **MAP-Overlay als eine Ebene über dem Editor-Content**, gespeist aus
   einer vom Editor gebauten Ziel-Liste (Key + Rechteck + Badge) — das
   Overlay kennt die Widgets nicht (Muster `CcControlLayer::setMapMode`).
   Nur ein Widget-Eingriff war nötig: `LooperWaveformStrip::getSegmentBounds`.

## Konsequenzen

- Der Grid-„Map"-Tab ist auf der Looper-Page ausgeblendet; dort führt der
  eigene MIDI-Tab.
- Zwei Bindungs-Tabellen können am selben Gerät dieselbe CC belegen
  (Grid und Looper wissen nichts voneinander) — Learn meldet das nicht.
  Bewusst in Kauf genommen; ein gemeinsamer Konflikt-Check wäre ein
  Editor-Callback und kann nachgezogen werden.
- **Ein Overlay über allem sperrt sich selbst ein** (Fund 20.07.2026:
  der MAP-Toggle lag darunter, nur ein Neustart half). Regel daraus: ein
  modaler Overlay-Modus braucht immer einen Weg hinaus. Umgesetzt sind
  drei: das Seitenpanel fängt nur ÜBER Zielen (MST/Output bleiben
  mappbar, Toggle und Liste bedienbar), die Transportleiste bleibt frei,
  und ESC beendet den Modus. Ein Seitenwechsel schaltet ihn ab, statt ihn
  zu verstecken.
