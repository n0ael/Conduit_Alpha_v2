# TransportBar & Metronom — Subsystem-Dossier
> Ausgelagert aus CLAUDE.md v4.6 §10.0, Juli 2026. Für Arbeiten an
> diesem Subsystem verbindlich wie die CLAUDE.md selbst (§1.1).

- **TransportBar** ersetzt die Modul-Button-Toolbar komplett: Play (Link
  Start/Stop-Sync), Tape (oo — Retro-Looper-Page, LED = Page offen ODER
  Loop spielt), Capture ⛶
  (Shift-Klick = Kanal-Panel), Fixed Length/Automate (persistierte
  Looper-Toggles), Tap-Tempo als Monitor (M4L-„TAP and CHANGE"-Modell:
  endloses Tappen misst NUR, Set-Kachel committet zur Session; Tap halten
  = Reset; Tap ▾ = Menü mit Auto-Commit ab Tap n fürs MIDI/OSC-Mapping +
  Reset-Haltedauer) + Nudge ±, Metronom ○●, Tempo/Position/Swing-Kacheln,
  Link ▾ (Menü: Start/Stop-Sync, Clock-Offset, Metronom-Ausgang),
  Page-Icons, „+"-Browser (Module + Presets), Undo (Shift-Klick = Redo),
  Save, ⚙, Skala.

- **Metronom** (`Source/Core/Metronome`): allocation-free Click NACH dem
  GraphFader auf ein wählbares Stereo-Paar; Beat-Grenzen sample-genau
  (floor-Überquerung, Muster 4.5), Downbeat oktavhöher, Disable lässt den
  Tail ausklingen. Bewusst kein isPlaying-Gate (Conduit läuft frei).
