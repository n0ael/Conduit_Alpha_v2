# ADR-Index (Architecture Decision Records)

ADRs liegen in `docs/adr/` und sind **append-only** — bestehende
Entscheidungen werden nicht umgeschrieben, nur durch neue ADRs
ergänzt/abgelöst. Ausgelagert aus CLAUDE.md §14 (v5.8).

| Nr. | Titel | Datei |
|---|---|---|
| 001 | SpscQueue als einziger Inter-Thread-Queue-Baustein + Repo-Rename | `001-spscqueue-und-repo-rename.md` |
| 002 | Grid-Page als Touch-Controller-Baukasten | `002-grid-touch-controller-baukasten.md` |
| 003 | Grid-Voice-Ausgabe: Quelle → Voice-Modell → austauschbare Sinks | `003-grid-voice-sinks.md` |
| 004 | Subsystem-Dossiers unter docs/ (+ Descope 10-Finger-Panic) | `004-subsystem-dossiers.md` |
| 005 | Subsystem-Invarianten als path-scoped Rules (.claude/rules/) | `005-path-scoped-rules.md` |
| 006 | MIDI-Rig-Subsystem: Klangerzeuger-/Controller-Profile, Transport, Threading | `006-midirig-subsystem.md` |
| 007 | SysEx-Empfang für den Hardware-Preset-Browser (Amendment zu 006 E6) | `007-sysex-empfang-preset-browser.md` |
| 008 | Node-Page Multipage: Seiten-View-Schicht, Gesten-Leiter, Performance-Modus | `008-node-page-multipage.md` |
| 009 | I/O als reguläre Browser-Module (§6.2-Regel entfällt) | `009-io-als-browser-module.md` |
| 010 | Looper-I/O: flexible Looper-Ein-/Ausgänge über Graph-Module | `010-looper-io.md` |
| 011 | Mono/Stereo-Adaptivität der Module (SKIZZE, eigener Meilenstein) | `011-mono-stereo-adaptivitaet-skizze.md` |
| 012 | Big Looper Out: Auto-Follow-Outputs, Send-Busse, Papierkorb | `012-big-looper-out.md` |
| 013 | Looper patch IN/OUT: Umbenennung (inkl. factoryIds) + Entfall des Mini-Out | `013-looper-patch-io-rename.md` |
| 014 | Soll-Layering & Kompositionsschicht (Source-Verzeichnisse, Include-Richtungen) | `014-soll-layering-kompositionsschicht.md` |
| 015 | Looper-Track-Mixer: XY-Distanz, Send-Level, freies Loop-Fenster | `015-looper-track-mixer-distanz.md` |
| 016 | Looper-MIDI-Map: eigene Bindungs-Instanz, Ziel-Kodierung, MAP-Overlay | `016-looper-midi-map.md` |
| 017 | Rollen-Modell & Commit-/Push-Autonomie | `017-rollen-commit-push-autonomie.md` |
