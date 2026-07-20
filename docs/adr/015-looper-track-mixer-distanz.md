# ADR 015 — Looper-Track-Mixer: XY-Distanz, Send-Level, freies Loop-Fenster

Status: Akzeptiert (20.07.2026) · Ersetzt nichts; erweitert ADR 012
(Send-Busse) und den Looper-Vollausbau.

## Kontext

Der Design-Handoff „Conduit Looper — Feature-Handoff" (20.07.2026)
ersetzt den Track-Fader durch einen Kanalzug: Stereo-Meter, XY-Panner
mit Distanz-Achse, Send-Kacheln mit Füllstand. Fachlich sind das drei
neue Engine-Fähigkeiten, die der Looper bisher nicht hatte:
stufenlose Send-Level (ADR 012 kannte nur eine An/Aus-Bitmaske), eine
Distanz-Wirkung nach „Monolake Distance" (R. Henke) und ein frei
verschiebbares Loop-Fenster (bisher nur ×2/÷2).

## Entscheidungen

1. **Send-LEVEL statt Bitmaske.** `TrackState.sendLevel[4]` (0..1) löst
   `sends` ab; die Audio-Seite hält 64 Atomics und addiert mit einer
   5-ms-Block-Ramp auf die Send-Busse. `getTrackSends`/`setTrackSends`
   bleiben als ABGELEITETE Masken-API bestehen (Farb-Resolver, Hash,
   Kompatibilität) — der Masken-Setter plättet dabei feine Level nicht.
   Alt-Dateien migrieren beim Laden Bit → 1.0 (die frühere Addition war
   Unity-Gain, also verhaltensgleich).
2. **Distanz pro Track (XY-Y).** Ein Zug NACH Gain/Pan/Mute, VOR Meter
   und Post-Bus: RBJ-High-Shelf + Tiefpass (beide TDF2), Mid/Side-Width,
   Vol-Kurve. Die Mathematik liegt JUCE-frei in
   `Source/Core/Looper/LooperDistance.h` (Muster LooperClipMath), die
   Koeffizienten berechnet der Audio-Thread EINMAL pro Block aus der
   geslewten Distanz — allocation- und lock-frei, Topologie wie das
   `line~`-Smoothing des Originals. Bei d ≈ 0 greift ein Bypass-Guard
   (Filterzustände zurückgesetzt, exakte Parität zum Signal ohne Distanz).
3. **Die Y-Achse ist der primäre Pegelweg** (User-Entscheidung): Vol Dump
   ist ab Werk AN und erreicht bei d = 1 EXAKT Stille (Equal-Power-Fade
   × dB-Neigung). Ein globaler `ySens` skaliert die Wirkung des Pad-Wegs
   (`d_eff = y · ySens`), der Puck bleibt absolut. Die Meter-Zeile bleibt
   zusätzlich vertikal ziehbar — die Gain-Geste des alten Faders.
4. **Post-Sends greifen VOR der Vol-Kurve ab** (nach Filter und Width).
   Damit bleibt ein Y-verlinkter Send bei voller Entfernung hörbar —
   gefiltert, aber nicht weggeduckt; genau die Erwartung „weiter weg =
   mehr Hall". Pre-Sends bleiben unberührt.
5. **Y-Link:** genau ein Send (oder keiner) folgt der Distanz;
   `sendTarget = max(Poti-Level, geslewte Distanz)`. Das Poti wirkt als
   Untergrenze — Puck nach oben fügt hinzu, nimmt nie weg.
6. **Freies Loop-Fenster (LEN/POS).** `setClipLengthBeats` /
   `setClipWindowOffsetBeats` setzen Länge und Offset STUFENLOS in
   Content-Beats (Untergrenze 50 ms); Sync-Raster (/1 /2 /4 /8,
   fensterweise) ist UI-Arithmetik darüber. Die Anwendung läuft
   unverändert über das Staged/Active-Protokoll — `computeCandidate`
   re-ankert beliebige Längen/Offsets bereits positions-kontinuierlich.
   `multiplyClipLength` bleibt als interne Shrink-Politik erhalten, die
   ÷2-Menüzeile entfällt (die Potis decken sie ab).
7. **Wrap-Crossfade folgt dem Fenster.** Er lag am Content-Ende und
   muss ans FENSTER-Ende (jedes verkürzte Fenster erzeugte sonst pro
   Durchlauf einen ungefadeten Splice). Zwei Quellen je nach Lage:
   Teilfenster blenden auf das hinter dem Fensterende WEITERLAUFENDE
   Material, das Vollfenster weiter auf den Lead-in (dort existiert kein
   Nachmaterial, und der Lead-in ist nur `crossfadeSamples` lang).

## Konsequenzen

- +64 Send-Atomics, +16 Distanz-Atomics, 4 Biquad-Zustände pro Track
  (~1 kB). Der Distanz-Zug kostet nur bei d > 0 und vorhandenen Voices;
  worst case 32 Koeffizienten-Sätze pro Block.
- Meter, Post-Bus, Patch-OUT-Track-Slots und Master hören dasselbe
  distanzierte Signal — „was du hörst" bleibt eine Wahrheit.
- Das Vollfenster-Playback bleibt bit-identisch zum Bestand (Wrap-Zone
  schnappt auf das Content-Ende, Blend-Quelle ist dort exakt der
  Lead-in) — abgesichert durch die sample-exakten Playback-Tests.
- Send-Farben sind frei wählbar (`ConduitColorPicker`); die Send-Slots
  am Looper patch OUT tragen sie statt der Mischung der summierten
  Clips — die Farbe identifiziert damit den Bus und bleibt beim
  Umstöpseln stabil.
