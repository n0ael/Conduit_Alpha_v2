#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace conduit::push
{

//==============================================================================
/**
    Vektorbasierte Icon-Bibliothek im Push-3-Stil (CLAUDE.md 10).

    Alle Symbole werden aus einem normierten 0..1-Quadrat in die Ziel-Bounds
    skaliert (größte einbeschriebene Quadratfläche, zentriert) — dadurch sind
    sie an jede Auflösung/DPI anpassbar, ohne Bitmaps. Minimalistisch, nicht
    fotorealistisch: dünne Strokes, runde Kappen, wie auf dem Controller.

    draw() kennt pro Icon die Fill/Stroke-Aufteilung (z. B. Metronom =
    Kreis-Outline + gefüllter Punkt); outlinePath() liefert die reine
    Geometrie für Tests und Sonderfälle.

    Zuordnung der Page-Icons (User-Entscheidung 2026-07-02, TouchLive 09.07.):
      pageMixer     (Balken)            → Mixer-Page
      pageTouchLive (Mini-Kanalzüge)    → TouchLive-Page (Ableton-Live-Remote,
                                          belegt vorerst den Clip-Slot)
      pageClip      (▷ im Rechteck)     → Clip-Page (Fugue-Machine-Sequencer,
                                          bekommt später wieder einen Slot)
      pageDevice    (|||)               → Device-Page (Patch-Canvas)
      pageGrid      (Ω-Schleife)        → Grid-Page (MPE-Touch-Controller)
*/
enum class Icon
{
    play,           // ▷  Link Start/Stop-Sync
    tapeLoop,       // oo mit Oberkanten-Strich — künftige Looper-Page
    captureFrame,   // ⛶  Capture Audio (Push: Capture MIDI)
    metronome,      // ○● Click an/aus
    plus,           // +  öffnet den Modul-Browser
    gear,           // ☼  Einstellungen (Push: Setup)
    nudgeLeft,      // ‹  Tempo-Nudge runter
    nudgeRight,     // ›  Tempo-Nudge hoch
    chevronDown,    // ▾  Dropdown-Pfeil (Link-Menü)
    pageMixer,      // Fader-Balken
    pageClip,       // ▷ im Rechteck
    pageDevice,     // |||
    pageGrid,       // Ω
    pageTouchLive,  // drei Mini-Kanalzüge (User-SVG TouchLive.svg, 09.07.2026)

    // Dev-Zeile des FxModulePanel (4.6) — Symbole statt Text (User 03.07.:
    // Text war bei 28px-Buttons unlesbar; Vektor-Icons skalieren immer)
    minus,          // −  letzten Wert-Button entfernen
    eye,            // 👁 Parameter sichtbar (Klick blendet aus)
    eyeOff,         // 👁 durchgestrichen — ausgeblendet (Klick blendet ein)
    valueButtons,   // ▦  2×2-Kacheln: Wert-Buttons statt Fader
    fader,          // ⊦  vertikaler Fader mit Griff: zurück zum Fader
    curve,          // ~  Bezier-Kurve/Range-Editor

    sharp,          // ♯  Session-Skala an/aus (Header, Ableton-Scale-Toggle)
    browserPanel,   // ▯▮ Browser-Panel rechts (Live-Icon gespiegelt)

    // Browser-Panel (Referenz-SVGs: Assets/svg-browser-icons/, von Hand
    // portiert — kein SVG-Runtime-Loading, CLAUDE.md 10)
    browserProjects,  // Ordner — PROJEKTE (gespeicherte Sessions)
    browserAudio,     // Waveform-Balken — AUDIO (Loops/One-Shots/Captures)
    browserCvControl, // Sinuswelle — MODULE ▸ CV/Control
    browserAudioFx,   // Drehknopf — MODULE ▸ AudioFX
    search,           // Lupe — Suchfeld
    chevronLeft       // ‹ Zurück (Breadcrumb-Header)
};

/** Zeichnet das Icon in die Bounds (Stroke-Breite skaliert mit der Größe). */
void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> bounds,
           juce::Colour colour);

/** Reine Icon-Geometrie, in die Bounds skaliert — für Tests (nicht leer,
    innerhalb der Bounds) und Custom-Rendering. */
[[nodiscard]] juce::Path outlinePath (Icon icon, juce::Rectangle<float> bounds);

} // namespace conduit::push
