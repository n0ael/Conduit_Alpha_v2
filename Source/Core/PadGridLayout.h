#pragma once
#include <juce_core/juce_core.h>

namespace conduit::grid
{

/** Reine Layout-/Mapping-Logik des isomorphen Pad-Rasters. Positionen sind
    auf [0,1] normalisiert (Ursprung oben-links, y wächst nach unten).
    Isomorph: +1 Halbton pro Spalte (nach rechts), +semitonesPerRow pro Reihe
    (nach OBEN). Testbar, keine UI-Abhängigkeit. */
class PadGridLayout
{
public:
    struct Config
    {
        int   cols                     = 8;
        int   rows                     = 4;
        int   lowestNote               = 48;    // C3, unterste Reihe, Spalte 0
        int   semitonesPerRow          = 5;     // Reihe darüber = +5 (Quart)
        float pitchBendRangeSemitones  = 48.0f; // == MpeEncoder-Default
        float semitonesPerPadWidth     = 2.0f;  // horizontale Bewegung: 1 Pad-Breite = 2 HT Bend
    };

    explicit PadGridLayout (const Config& cfg = {}) noexcept;

    /** Pad-Index [0, cols*rows) aus normalisierter Position, oder -1 außerhalb.
        Index = row*cols + col, row 0 = OBERSTE Zeile. */
    int   padIndexAt   (float normX, float normY) const noexcept;
    /** MIDI-Note eines Pad-Index (unterste Reihe = lowestNote, +1/Spalte, +semitonesPerRow/Reihe hoch). */
    int   noteForPad   (int padIndex) const noexcept;
    /** Kombination: Note direkt aus Position, -1 außerhalb. */
    int   noteAt       (float normX, float normY) const noexcept;
    /** Pitch-Bend in Halbtönen aus horizontaler Bewegung, clamp ±range. */
    float pitchBendSemitones (float startNormX, float currentNormX) const noexcept;
    /** Erste Ausdrucksachse [0,1] aus vertikaler Position INNERHALB des Pads,
        auf dem der Finger aufsetzte (0 = Pad-Unterkante, 1 = Pad-Oberkante) —
        entkoppelt Ausdruck von der Note-Wahl. */
    float expressionInPad (int padIndex, float normY) const noexcept;

    int cols() const noexcept { return config.cols; }
    int rows() const noexcept { return config.rows; }

private:
    Config config;
};

} // namespace conduit::grid
