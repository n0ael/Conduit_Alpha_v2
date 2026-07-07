# FX-Chassis-Standard — Subsystem-Dossier
> Ausgelagert aus CLAUDE.md v4.6 §4.6, Juli 2026. Für Arbeiten an
> diesem Subsystem verbindlich wie die CLAUDE.md selbst (§1.1).

Jedes Audio-FX-Modul erbt von `ProcessorModule` und bekommt damit AUTOMATISCH
das Standard-Chassis. Neue FX-Module implementieren NUR `prepareCore()`/
`processCore()` (reine Stereo-Audio-Sicht, Kanäle 0..1) und liefern ihre
DSP-Parameter als `ChassisParamDesc`-Liste an den ProcessorModule-Konstruktor.
NIE `prepareToPlay`/`processBlock`/`appendParametersTo`/`getParameterTarget`
selbst überschreiben (final).

**Das Chassis stellt bereit (nicht optional, nicht nachbauen):**

- **Input-/Output-Gain** (`input_gain`/`output_gain`, −60..+6 dB, −60 = exakt
  Stille, role="chassis"), SmoothedValue 5 ms. Signal-Reihenfolge:
  noteBlockBegin → CV lesen → In-Gain → In-Meter → processCore → Out-Gain →
  Out-Meter → Link-Tap.
- **2×2-Kanal-LevelMeter** (eigene Instanzen im Modul; UI liest transient pro
  Tick über `GraphManager::getModuleFor` — nie Meter-Pointer cachen, 5.3).
- **Link-Audio-Send-Tap** (LinkSendTaps, Kanal-Name = moduleId, Node-Property
  `linkSendEnabled` = Patch-Zustand, undo-fähig via
  `GraphManager::setLinkSendEnabled`); Chassis implementiert ILinkAudioClient
  + IClockSlave — Injektion/Rename/Phase-1-Retire übernimmt der GraphManager.
- **Pro DSP-Parameter ein CV-Eingang**: Kanal-Layout FEST Audio 0..1, CV 2..N
  (CV-Kanal von Parameter i = 2+i, `ChassisSchema::cvChannelForParam`).
  **CV-Richtungs-Modell** (User-Entscheidung 07/2026): CV blockkonstant als
  Blockmittel des BETRAGS (Gleichrichtung VOR der Mittelung — bipolare
  Quellen werden zur Hüllkurve), die Richtung bestimmt allein der
  Attenuverter `{param}_cv_amt` (−1..+1, role="cvAmount"): positiv = vom
  Fader-Wert nach oben, negativ = nach unten.
  `effective = clamp(base + |cv|·amt·(userMax−userMin), userMin, userMax)`.
- **Control-Linking** (modulintern): `linkSource`/`linkAmount`/`linkCurve`
  pro dsp-Parameter — das Ziel folgt der normalisierten Stufe-1-Quelle als
  interne Modulation (folgt auch OSC/CV der Quelle, Ziel-Fader bleibt
  stehen). DSP zweistufig: Stufe 1 = base+CV, Stufe 2 = Links auf Stufe-1-
  Basis → Zyklen (A↔B) sind harmlos. Optionale Link-Response
  (`ChassisSchema::LinkResponse`: Bezier-Form + Start-/Endwert — FALLENDE
  Responses drehen die Richtung direkt in der Kurve, z.B. Auto-Gain/
  Gain-Matching). APIs: `setParameterLink`/`setParameterLinkCurve`.

**Schema-Regeln:**

- Parameter-Property `role`: "dsp" | "chassis" | "cvAmount" — die UI layoutet
  danach; OSC-Adressen bleiben kanonisch /conduit/processor/{moduleId}/{paramId}.
- `userMin`/`userMax`/`uiHidden`/`curve`/`linkSource`/`linkAmount`/`linkCurve`
  sind Patch-Zustand pro dsp-Parameter (Dev-Modus): der Fader nutzt die
  User-Range + Bezier-Kurve (reines UI-Mapping via CurvedSlider — im Tree
  steht IMMER der echte Wert); CV/Links wirken IM User-Bereich.
- Range-Edits clampen den Wert in DERSELBEN Undo-Transaktion
  (`setParameterUserRange`); `uiHidden` trennt CV-Kabel des Parameters in
  derselben Transaktion (keine Phantom-Modulation) und ändert NIE das
  Bus-Layout/numInputChannels (sonst Graph-Rebuild pro Klick).
- Bezier-Kurven: Kontrollpunkte via `ChassisSchema::parseCurve` auf [0,1]
  geclamped → x(t) UND y(t) monoton, Mapping eindeutig invertierbar.
- Modul-Typ-Defaults (`ModuleUiDefaults`, App-Zustand, Muster MeterSettings):
  „als Standard" im Dev-Modus sichert die dsp-Overrides pro factoryId;
  Overlay greift NUR bei Neu-Anlage (`addModuleNode`) — Presets/Patches
  gewinnen immer.
- Chassis-Versionierung: stateVersion ≥ 2; `ChassisSchema::migrate` läuft
  idempotent in `GraphManager::normalizeNode` für alle Processor-Nodes
  (Gains/Attenuverter/role ergänzen, Kanäle 0/1 stabil — Kabel überleben).

**UI:** `FxModulePanel` ist die Pflicht-Oberfläche aller Processor-Nodes
(Auswahl über `type == "Processor"`, nicht factoryKey) — links In-Zug
(GainFaderMeter: Fader + dB-Skala + Stereo-Meter + Clip-Reset), Mitte pro
sichtbarem dsp-Parameter eine vertikale Fader-Spalte (Titel / CurvedSlider /
Attenuverter-Knob / CV-Port), rechts Out-Zug + LINK-Button + Status-LED.
Dev-Modus-Toggle sitzt im Node-Header (transient pro Kachel, KEIN Patch-/
App-Zustand); der ~-Button jeder Spalte öffnet den CurveEditor (CallOutBox):
Tabs Fader/Link, draggbare Range-Endpunkte, Min/Max-Felder, Link-Quelle +
Amount. CV-Port-Anker delegiert `NodeComponent::getPortCentre` an
`FxModulePanel::cvPortCentre` (Kanäle ≥ 2).
