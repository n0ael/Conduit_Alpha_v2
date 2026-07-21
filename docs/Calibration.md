# CV-Hardware-Kalibrierung — Subsystem-Dossier
> Ausgelagert aus CLAUDE.md v4.7 §8.1–8.3, Juli 2026. Für Arbeiten an
> diesem Subsystem verbindlich wie die CLAUDE.md selbst (§1.1).
> Die interne Spannungs-Konvention (float ±1.0 == Full Scale,
> `fullScaleVolts`) bleibt in CLAUDE.md 8.0 — sie gilt app-weit.

Motivation: DC-coupled Interfaces (ES-3, ESX-8CV etc.) haben
hardware-spezifische DC-Offsets und Gain-Abweichungen. `0.0f` digital
≠ `0.000V` analog → Out-of-Tune bei 1V/Oct.

**Spannungs-Konvention — Beispiele (Kern-Regel bleibt CLAUDE.md 8.0):**

- Intern gilt: float ±1.0 == Full Scale des Interfaces. Bei ±10-V-Hardware
  (ES-Serie) entspricht 1 V also 0.1f; Eurorack-Gate-High (+5 V) = 0.5f.
- Module rechnen IMMER in dieser normalisierten Skala; die Umrechnung in
  echte Volt passiert ausschließlich im HardwareIOModule über das
  CalibrationProfile (dcOffset/gainTrim) plus `fullScaleVolts` pro
  Interface (Profil-Feld, Default 10.0).
- UI zeigt Volt an, speichert normalisiert.

## 8.1 CalibrationProfile (per Interface)

```cpp
struct CalibrationProfile {
    juce::String interfaceId;      // primärer Key: exakter Device-Name
    juce::String interfaceIdPrefix; // Fallback: Prefix ohne Suffix wie " (2)"
    float        dcOffset;
    float        gainTrim;
};

// Im HardwareIOModule::processBlock() — allocation-free:
float calibrated = (rawValue + profile.dcOffset) * profile.gainTrim;
```

**Profile-Matching bei USB-Reconnect (Reihenfolge):**
1. Exakter Name-Match (`"ES-3"` == `"ES-3"`)
2. Prefix-Match (ignoriert Suffix: `"ES-3 (2)"` → matched `"ES-3"`)
3. Kein Match → UI zeigt Kalibrierungs-Warnung, Profil auf Neutral (`dcOffset=0, gainTrim=1`)

Profile sind kanalspezifisch, persistent im ValueTree, user-adjustierbar.

## 8.2 CVTunerModule (AnalysisModule)

Natives Kalibrierungswerkzeug analog zu Ableton CV Tools — ohne M4L-Abhängigkeit.

**Ablauf:**
1. Gibt bekannten Referenz-CV-Wert aus (konfigurierbar: 0V, 1V, 2V, 5V) via ES-3/ESX-8CV
2. Misst Rückweg via ES-6 Eingang
3. Berechnet `dcOffset` und `gainTrim` aus Differenz
4. Schreibt `CalibrationProfile` in ValueTree → sofort aktiv
5. Wiederholbar pro Kanal

```cpp
class CVTunerModule : public AnalysisModule {
    // Schreibt NUR in ValueTree (CalibrationProfiles)
    // Niemals direkt in Audio-Pfad
    // Messung läuft auf separatem Analyse-Thread
};
```

## 8.3 Latenz-Trim für CV-Ausgänge

Hardware-Realität (v1-erprobt): Modulsysteme brauchen ms-genauen Versatz.
- Pro CV-Ausgangskanal: `shiftMs` (±50 ms), zusätzlich globales
  `globalShiftMs` — beide als Beat-Offset im Audio-Thread eingerechnet.
- Gehört ins CalibrationProfile bzw. den Kanal-State, user-adjustierbar.
