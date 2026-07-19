# ADR 010 — Looper-I/O: flexible Looper-Ein-/Ausgänge über Graph-Module

Status: Beschlossen 18.07.2026 (User-Entscheidungen per Rückfrage) ·
Betrifft: Looper-Subsystem (Rule `looper`, docs/Looper.md), CaptureService,
GraphManager, LooperBank, Quellen-Combo der Looper-Page

## Kontext

Die Looper-Quellenliste mischte bisher Master, Hardware-Paare,
Ausgangs-Paar-Taps (`out:{paar}`) und Link-Receive-Taps; die Summe aller
Looper ging an genau EIN Stereo-Anker-Paar; alles war hart stereo
(`pair*2`-Arithmetik, 2-Kanal-Clips). Wunsch (User 18.07.2026): schlanke
Liste, beliebige Graph-Signale loopbar, Looper-Signale zurück in den
Graph, Master optional, echte Mono-Aufnahmen.

## Entscheidung

1. **Zwei neue Graph-Module** (reguläre Browser-Module, `ModuleType::io`):
   - **Looper In** (`looper_in`): dynamische Eingangs-Slots (mono|stereo,
     benennbar, `<Inputs>`-Schema des Link-Audio-Send). Pro Slot virtuelle
     Capture-Kanäle (`{moduleId}/{name}` bzw. `…_l/_r`) — Slots erscheinen
     ZUOBERST in der Quellen-Combo (Schlüssel `tap:{basisname}`). Output =
     Pass-Through (Muster CaptureTapModule).
   - **Looper Out** (`looper_out`): dynamische Abgriff-Slots
     (`<Outputs>`-Schema): Ziel Master|Looper 1–4 × Modus
     stereo|sum|left|right × Pre/Post-Fader (User: pro Ausgang
     umschaltbar). Default: Master + Looper 1–4 stereo post.
2. **Quellenliste komplett ersetzt:** ohne Looper-In-Modul nur
   Interface-Eingänge; Mono/Stereo der HW-Einträge folgt dem ∥-Pairing
   der ChannelNames (gepaart = `hw:{paar}`, ungepaart = NEU `hwm:{kanal}`).
   `master`-/`out:`-/fremde `tap:`-Einträge entfallen (Legacy-Keys
   resolven weiter bzw. zu −1, kein Crash). Die `out{p}`-Ausgangs-Taps
   sind ersatzlos entfernt — Ausgangs-Signale loopt man per Kabel ins
   Looper-In-Modul.
3. **Mono-Aufnahme:** Mono-Quelle (rechts = −1) ⇒ 1-Kanal-Clip (halber
   RAM), Playback speist beide Seiten, Export schreibt EINE Datei ohne
   Suffix.
4. **Render-Split der LooperBank:** `renderBlock()` läuft VOR
   `graph.processBlock` (Playback liest nur committete Clips) und füllt
   pro Looper Pre-/Post-Fader-Busse + Master-Mix; das Looper-Out-Modul
   liest sie im SELBEN Callback über `getAudioView()` — sample-aligned,
   ohne Block-Latenz. `mixToOutput()` addiert den Master-Mix NACH dem
   Graph aufs Anker-Paar (Feedback-Freiheit des Master-Taps bleibt).
5. **Master-Optionen:** Anker −1 = „Kein Master-Out" (TransportSettings
   erlaubt −1); pro Looper `sendMaster`-Flag (LooperSettings, Default an)
   — User: beides unabhängig von Looper-Out-Abgriffen (parallel).
6. **Re-Materialisierung gefadet:** Slot-Umbauten (numChannels-Property
   an `looper_in`/`looper_out`) re-materialisieren den Node im NÄCHSTEN
   gefadeten Swap (`pendingRematerialize` im GraphManager) — anders als
   der harte Endpunkt-Pfad (Gerätewechsel), weil das System spielt.
7. **Kapazität:** `MAX_VIRTUAL_CHANNELS` 16 → 64 (Registry-Einträge sind
   billig; Puffer entstehen nur für registrierte Slots; effektive Grenze
   bleibt `MAX_CAPTURE_CHANNELS` 64 minus Hardware-Kanäle).

## Begründung

- EIN Mechanismus fürs Loopen beliebiger Signale (Kabel → Looper-In)
  statt wachsender Sonderfälle in der Combo (out-Paare, Link-Gruppen).
- Der Render-Split vermeidet die 1-Block-Latenz eines Ring-Handoffs und
  hält Master-Pfad und Modul-Abgriff phasenstarr.
- Mono-Clips: Loop-Export von Mono-Quellen erzeugt echte Mono-Dateien
  (User-Begründung), halber RAM im Budget.
- Das ∥-Pairing ist bereits die Mono/Stereo-Wahrheit des Node-Editors —
  die Combo folgt derselben Quelle (unsichtbar, konsistent).

## Konsequenzen & Auflagen

- LooperBank-API: `process()` bleibt als Wrapper (Tests/Alt-Kontrakt).
- `AudioView`-Pointer gelten NUR im selben Audio-Callback nach
  `renderBlock()`; die Bank überlebt den Graph
  (EngineProcessor-Deklarationsreihenfolge, Muster CaptureService).
- Slot-/Namensänderungen ändern die `tap:`-Schlüssel — gespeicherte
  sourceKeys verlieren dann ihre Auflösung (Combo zeigt leer; gleiche
  Semantik wie ein Modul-Rename, dokumentierter Alpha-Caveat).
- Re-Sampling-Ketten (Looper Out → FX → Looper In als Quelle) sind
  erlaubt und rückkopplungsfrei (Playback liest nur committete Clips).
- Die Mono/Stereo-ADAPTIVITÄT der FX-Module (Auto-Mono-Processing,
  Mono-in→Stereo-out für Raum-FX, Mini-Fenster Summe/L/R) ist BEWUSST
  ausgeklammert → ADR 011 (Skizze, eigener Meilenstein).
