---
paths:
  - "Source/**/*Calibrat*"
  - "Source/**/*CVTuner*"
  - "Source/**/*HardwareIO*"
  - "Source/Core/ChannelNames.h"
  - "Source/Core/EngineProcessor.*"
  - "Tests/**/*Calibrat*"
  - "docs/Calibration.md"
---

# Rule: calibration — CV-Hardware-Kalibrierung (CLAUDE.md §8)

**Pflichtlektüre vor jeder Kalibrierungs-Arbeit: docs/Calibration.md**
(CalibrationProfile, CVTunerModule-Ablauf, Latenz-Trim — Nummern
8.1–8.3). Die Spannungs-Konvention (±1.0 == Full Scale,
`fullScaleVolts`) bleibt app-weit in CLAUDE.md 8.0.

- CalibrationProfile (8.1): `interfaceId` (exakter Device-Name,
  primärer Key) + `interfaceIdPrefix` (Fallback ohne Suffix wie " (2)")
  + `dcOffset` + `gainTrim`; im HardwareIOModule allocation-free:
  `(raw + dcOffset) * gainTrim`. Matching bei USB-Reconnect: exakt →
  Prefix → kein Match = Neutral-Profil + UI-Warnung. Profile sind
  kanalspezifisch, persistent im ValueTree, user-adjustierbar.
- CVTunerModule (8.2): schreibt NUR CalibrationProfiles in den
  ValueTree, nie in den Audio-Pfad; Messung auf separatem
  Analyse-Thread; wiederholbar pro Kanal.
- Latenz-Trim (8.3): pro CV-Ausgangskanal `shiftMs` (±50 ms) plus
  globales `globalShiftMs`, beide als Beat-Offset im Audio-Thread
  eingerechnet — gehört ins CalibrationProfile bzw. den Kanal-State,
  user-adjustierbar.
