# Airwindows-Portierung — Notizen

> Quelle (read-only, gitignored): `Third-Party/airwindows`, jeweils `plugins/LinuxVST/src/<Name>`
> Regeln: CLAUDE.local.md (Session-Scope) + CLAUDE.md v4.2

## Status

> Status-Konvention: „grün" erst nach tatsächlich gelaufenem Nachweis
> (Build warnings-clean + drei DoD-Tests + ASan), nie vorab.

| Plugin | Status | Parameter (id → Original-Name, Default) | Besonderheiten |
|---|---|---|---|
| Slew | **grün** (verifiziert 02.07.2026, Schritt 3: Debug `/W4 /WX` clean, 3 DoD-Tests + ASan grün) | `clamping` → "Clamping", 0.0 | Kein fpd-Dither am Ausgang (Altbestand 2011) — `setDitherEnabled` ist wirkungslos. `fpd` ist im Original **uninitialisiert** und wird nie advanced (konstanter Denormal-Guard); der Port seedet deterministisch, Verhalten sonst 1:1. |
| Spiral | **grün** (verifiziert 02.07.2026, Schritt 3: Debug `/W4 /WX` clean, 3 DoD-Tests + ASan grün) | — (0 Parameter) | Zustandslos bis auf fpd-Xorshift. |
| Density | **grün** (verifiziert 02.07.2026, Schritt 3: Debug `/W4 /WX` clean, 3 DoD-Tests + ASan grün) | `density` → "Density", 0.2 (= interne Skala 0.0) · `highpass` → "Highpass", 0.0 · `out_level` → "Out Level", 1.0 · `dry_wet` → "Dry/Wet", 1.0 | `rand()`-Seed des Originals durch deterministischen Seed ersetzt (CLAUDE.md 3.1). |

**Schritt-3-Verifikationsnachweis (isolierter Harness, MSVC 19.50 / VS 2026, C++20):**
`ConduitAirwindows` + `ConduitAirwindowsTests` standalone gebaut (`add_subdirectory` per absolutem Pfad, JUCE 8.0.4 + Catch2 v3.7.1 via FetchContent — Root-CMakeLists unangetastet). Debug: 10 Testfälle / 2565 Assertions grün, `/W4 /WX` clean. ASan (`/fsanitize=address`, Runtime-DLL im PATH): dieselben 10 Testfälle / 2565 Assertions grün, keine Reports. Je Plugin die 3 DoD-Tests (Null-Test, Blockgrößen-Invarianz 64/512, Parameter-Sweep ohne NaN/Inf/Denormals) plus 1 Registry-Test.

## Mechanische Anpassungen (gelten für alle Ports, kein DSP-Change)

- Portierungsbasis ist der `processReplacing`-Körper (float I/O, interne
  Berechnung double — die Double-Pfade der Originale bleiben erhalten,
  Konvertierung nur an den Sample-Grenzen per explizitem `(float)`-Cast,
  Warnings-clean unter `/W4 /WX`).
- VST2-Gerüst (AudioEffectX, Chunks, canDo, Programs) entfällt; Parameter-
  Metadaten als statische `ParameterInfo`-Tabellen, Laufzeitwerte als
  `std::atomic<float>` (Snapshot am Blockanfang → blockkonstant wie im
  Original).
- fpd-Dither-Add hinter `ditherOn()` (Property `ditherEnabled`, Default OFF);
  der Xorshift-Advance läuft immer — identisch zum
  `processDoubleReplacing`-Pfad der Originale.
- fpd-Seeds deterministisch statt `rand()`; die Original-Untergrenze 16386
  bleibt erhalten (`AirwindowsPlugin::prepare`).
- `math.h`-Aufrufe → `std::`-Äquivalente (`std::sin`, `std::fabs`, `std::pow`,
  `std::frexp`) — numerisch identisch.
- `*in1++;` (Deref ohne Wirkung) → `++in1;` etc.
- `juce::ScopedNoDenormals` liegt im Basis-`process()` (CLAUDE.local.md).

## Manuell nötig

*(noch keine — Kandidaten mit Latenz/Lookahead/Allokationen im Prozesspfad
hier listen statt automatisch portieren)*

## Definition of Done (Referenz)

1. Baut isoliert als Teil von `ConduitAirwindows` (C++20, Warnings clean)
2. Drei Catch2-Tests grün: Null-Test (Dither off), Blockgrößen-Invarianz
   (64 vs. 512), Parameter-Sweep ohne NaN/Inf/Denormals
3. ASan-Lauf sauber
4. Eintrag in dieser Datei
