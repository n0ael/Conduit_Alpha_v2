# ADR 011 — Mono/Stereo-Adaptivität der Module (SKIZZE, nicht beschlossen)

Status: Skizze 18.07.2026 — eigener späterer Meilenstein (User-Scope-
Entscheidung bei Looper-I/O, ADR 010). VOR der Umsetzung ausformulieren
und mit dem User die offenen Fragen klären.

## Problem

FX-Module sind hart 2-in/2-out: Bei Mono-Zuspielung läuft der rechte
Kanal stumm mit (kein CPU-Sparpfad, Meter bleiben stereo, Reverb/Chorus
liefern kein echtes Stereo aus Mono). Beim Ziehen einer Stereo-Quelle
auf einen mono dargestellten Eingang fehlt ein Auto-Zweitkabel.

## Zielbild (User 18.07.2026)

- Signalbreite ist ein ABGELEITETER Zustand aus den eingehenden Kabeln —
  kein Schalter, kein Patch-Property: nur Kanal 0 belegt ⇒ Mono-Modus.
- Mono-Modus eines FX: mono prozessieren (CPU sparen), Meter mono,
  Ausgang als EIN Mono-Stecker dargestellt.
- Inhärent verbreiternde FX (Reverb, Chorus, …): mono-in ⇒ IMMER
  stereo-out; per Dwell-Geste (existiert) weiterhin nur eine Seite
  abgreifbar.
- Breite propagiert durch die Kette; die Kabel machen sie sichtbar.
- Stereo-Quelle auf Mono-Ziel: Mini-Fenster (CallOutBox, Vorbild
  HW-MIDI-Steuerung) mit „Summe / L / R / (wo sinnvoll) Eingang
  aufweiten" — beim Aufweiten entsteht das zweite Kabel automatisch
  und sichtbar.

## Vorhandene Bausteine (Inventur 18.07.2026)

- Kabel sind Mono-Connections; Stereo = span-2-Paar (`addConnectionPair`,
  Paar-weises Trennen, Dwell→Mono-Override, ∥-Pairing am audio_input).
- FX-Chassis-Bus bleibt 2/2 (Bus-Umbau würde Graph-Rebuild erzwingen) —
  Adaptivität ist ein LAUFZEIT-Verhalten im processBlock, kein Bus-Umbau.

## Offene Punkte für die Ausarbeitung

1. Klassifizierung ALLER FX (54 Airwindows + künftige): `monoCapable`
   vs. `inherentlyStereo` — Registry-Feld, nicht pro Modul hartkodiert.
2. Prüfen, welche Airwindows-Cores mit einem 1-Kanal-Lauf korrekt sind
   (viele Algorithmen sind kanal-symmetrisch, einige nicht).
3. Breiten-Ableitung: aus dem Connections-Tree (Message Thread) →
   atomic ans Modul; Kaskaden-Update bei Kabel-Änderungen.
4. Meter-Umschaltung (GainFaderMeter mono), Port-Darstellung (span
   folgt Breite), Kabel-Optik.
5. Stereo→Mono-Mini-Fenster: wo genau verankert (Drop-Punkt), und wie
   verhält sich „Eingang aufweiten" bei fix-mono Utilities.
6. Interaktion mit Looper-In-Slots (dort ist die Breite EXPLIZIT
   konfiguriert — das Mini-Fenster bietet zusätzlich „Slot aufweiten").
