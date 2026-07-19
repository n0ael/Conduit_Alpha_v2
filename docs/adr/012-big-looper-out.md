# ADR 012 — Big Looper Out: Auto-Follow-Outputs, Send-Busse, Papierkorb

Status: Akzeptiert (19.07.2026) · Ersetzt nichts; baut auf ADR 010 auf.

## Kontext

ADR 010 lieferte `looper_out` mit manuell bestückten Abgriff-Slots
(Master | Looper 1–4 × stereo/sum/left/right × Pre/Post). Der User will
als STANDARD-Ausgangsmodul ein „Big Looper Out", das die komplette
Looper-Struktur in den Graph spiegelt — pro Track ein Clip-Out, dazu
Bus-, Send- und Master-Outs — und sich selbstständig an die Struktur
anpasst. Sends existierten in der Engine nicht.

## Entscheidungen

1. **Neues Modul `looper_big_out`** („Looper Out" im Browser), Ausgänge
   folgen AUTOMATISCH der Looper-Struktur (numLoopers/numTracks aus den
   LooperSettings) — keine manuelle Slot-Pflege. Das bisherige
   `looper_out` heißt jetzt **„Looper Out Mini"** (factoryId unverändert,
   Preset-Kompatibilität) und bleibt der Weg für gezielte Einzel-Abgriffe
   (Pre/Post, Mono-Modi).
2. **Slot-Ordnung** (alle Slots fix stereo, Breite 2): Track-Outs
   geflattet Looper-major (`L1·T1 … L1·Tn₁, L2·T1 …`) → Bus-Outs
   (Post-Fader-Bus je Looper) → **Send 1–4 (IMMER alle 4** — stabile
   Kanal-Indizes minimieren Kabel-Geschiebe) → Master. Track-Outs sind
   POST-Fader: Mono-Clips kommen dank der Panning-Sektion stereo heraus
   (User-Anforderung).
3. **Send-Busse in der LooperBank** (`maxSends = 4`, `sendBus[4][2]`):
   pro Track eine Bitmaske (S1–S4) + PRE/POST-Abgriff, beides in den
   LooperSettings (`TrackState.sends`/`sendPre`) persistiert und via
   `applyLooperSettings` in Bank-Atomics gespiegelt. UI: SND-Kachel im
   Track-Strip → `LooperSendDialog` (CallOutBox).
4. **trackBus ersetzt den geteilten Render-Scratch:** jede (l,t)-Zelle
   rendert in ihren eigenen preallozierten Stereo-Bus; die bestehende
   In-Place-Fader-Kette hinterlässt dort gratis das Post-Fader-Signal.
   `AudioView` += `track[4][4][2]` + `send[4][2]` (View-Pointer-Regel
   unverändert: nur im selben Callback).
5. **Auto-Follow im GraphManager:** `setLooperStructure` (Cache) +
   `syncLooperBigOutConfigs` — bei Struktur-Diff werden Kabel über
   SPEC-IDENTITÄT remappt (verschwundene Slots verlieren ihr Kabel,
   überlebende Kanäle werden alt→neu umgeschrieben — Kanal-Arithmetik
   reicht NICHT: ein entfernter Track von Looper 2 verschiebt die
   geflatteten Offsets aller späteren Slots), dann `<Outputs>` frisch
   geschrieben und EXPLIZIT gefadet re-materialisiert (bei gleicher
   Kanalzahl feuert kein numOutputChannels-Listener). Undo-Policy:
   nullptr — die Struktur ist App-Zustand außerhalb des Undo-Trees.
   Call-Sites: applyLooperSettings, addModuleNode (Neu-Anlage folgt
   sofort), loadPreset/setStateInformation (Reconcile), Force-Delete
   (synchron).
6. **Delete-Gating + Papierkorb:** Looper-/Track-Delete läuft nur noch
   direkt, wenn weder Clips noch Big-Out-Kabel betroffen sind; sonst
   X/OK-CallOut (`LooperDeleteConfirmDialog`). OK = Force-Delete:
   Clips werden aus dem SessionModel DETACHT (kein `bank.deleteClip` —
   die Bank bleibt Besitzerin, `ramBytesUsed` zählt weiter), betroffene
   Kabel Spec-relativ eingesammelt und entfernt, Struktur geschrumpft.
   Alles landet im **`LooperTrashCan`** (~180 s): ↺-Kachel im
   Looper-Header stellt den jüngsten Eintrag wieder her (Struktur
   nachwachsen, Clips reattachen, Kabel aus der DANN gültigen Slot-Liste
   neu anlegen); die letzten 30 s faded die Kachel zu Rot, beim Ablauf
   flackert sie kurz und verschwindet. NICHT der UndoManager: Clips
   leben engine-seitig, die Struktur in den LooperSettings.
   `prepareToPlay` leert den Papierkorb VOR `bank.prepare` (der Store
   wird freigegeben — Pointer wären dangling; kein Double-Free, da kein
   deleteClip aussteht).

## Konsequenzen

- Max. 25 Slots / 50 Kanäle pro Big-Out-Node (4×4-Vollausbau); die
  Kachel ist read-only (keine +/×/PRE-Buttons), Sektions-Trenner
  gliedern Tracks | Busse | Sends | Master.
- +40 preallozierte Bus-Vektoren (~0,5 MB @ 2048er-Block) — Render-Pfad
  bleibt allocation-/lock-frei.
- Papierkorb-Restore ist best effort: ist die Struktur-Position
  inzwischen belegt, bleibt der Eintrag liegen (Fenster erneuert);
  verschwundene Nodes/Slots werden beim Kabel-Restore übersprungen
  (Toast nennt die Zahl).
