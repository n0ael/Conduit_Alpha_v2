#pragma once
#include <juce_core/juce_core.h>

namespace conduit::grid
{

/** In-Tune-Anker des Pitch-Bends (Block B1, Push-3-Paradigma):
    pad = Pad-Zentrum ist "in tune", der Finger bendet ABSOLUT nach Distanz
    zum Zentrum — dieselbe Position erneut anschlagen ergibt denselben Pitch
    (Default). finger = Aufsetzpunkt ist 0 Bend (bisheriges Verhalten). */
enum class InTuneLocation { pad, finger };

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
        float semitonesPerPadWidth     = 1.0f;  // horizontale Bewegung: 1 Pad-Breite = 1 HT Bend
                                                 // (Block B3: aufs isomorphe Raster kalibriert —
                                                 //  n Spalten Wischweg = n Halbtöne; vorher 2.0,
                                                 //  User-Befund C2→D3 statt G#2)
        float yRangeNorm                = 0.5f;  // normalisierte Wischstrecke für die volle [0,1]-Auslenkung
                                                   // (0.25 je Richtung ab neutral) — verdoppelte Reichweite ggü. der Pad-Höhe
        float inTuneWidthPercent        = 20.0f; // Block B2: Breite der In-Tune-Zone in % der
                                                   // Pad-Breite (Totzone um jeden Anker; dazwischen
                                                   // läuft der Bend entsprechend steiler).
                                                   // TODO(design): Default-Feinabstimmung mit dem User.
    };

    // Clang lehnt eine verschachtelte Config mit In-Class-Defaults als
    // Default-Argument ab ("needed within definition of enclosing class
    // ... outside of member functions") — Default-Ctor delegiert stattdessen
    // in der .cpp, wo PadGridLayout schon vollständig ist.
    PadGridLayout() noexcept;
    explicit PadGridLayout (const Config& cfg) noexcept;

    /** Pad-Index [0, cols*rows) aus normalisierter Position, oder -1 außerhalb.
        Index = row*cols + col, row 0 = OBERSTE Zeile. */
    int   padIndexAt   (float normX, float normY) const noexcept;
    /** MIDI-Note eines Pad-Index (unterste Reihe = lowestNote, +1/Spalte, +semitonesPerRow/Reihe hoch). */
    int   noteForPad   (int padIndex) const noexcept;
    /** Kombination: Note direkt aus Position, -1 außerhalb. */
    int   noteAt       (float normX, float normY) const noexcept;
    /** Pitch-Bend in Halbtönen aus horizontaler Bewegung — LINEAR, ohne
        In-Tune-Zone (Akkord-Latch-Verschiebung, moveLatchedBy). NICHT
        geklemmt — Werte über ±range durch große Bewegungen sind gewollt
        (die pitchBendAxis/der Encoder klemmen erst am Ausgang,
        CLAUDE.md 14 ADR). */
    float pitchBendSemitones (float startNormX, float currentNormX) const noexcept;

    /** Pitch-Bend in Halbtönen relativ zu einem In-Tune-ANKER (Block B1/B2):
        Treppen-Kennlinie mit In-Tune-Zonen (Breite inTuneWidthPercent der
        Pad-Breite) um den Anker und um jedes ganzzahlige Pad-Raster darüber
        hinaus — der nächste Pad-Mittelpunkt liegt exakt bei
        ±semitonesPerPadWidth, dazwischen läuft der Bend entsprechend
        steiler. Stetig, monoton, NICHT geklemmt. Bei inTuneWidthPercent = 0
        exakt die lineare Kennlinie von pitchBendSemitones. */
    float pitchBendFromAnchor (float anchorNormX, float currentNormX) const noexcept;

    /** Normalisierte x-Position des Pad-Zentrums (In-Tune-Anker im
        pad-Modus, Block B1). */
    [[nodiscard]] float padCentreNormX (int padIndex) const noexcept;
    /** Erste Ausdrucksachse aus vertikaler Bewegung relativ zum Aufsetzpunkt.
        Neutral (0.5) beim Aufsetzen; nach oben > 0.5, nach unten < 0.5.
        NICHT geklemmt — Werte über 1 / unter 0 durch Weiterwischen sind
        gewollt (der Sink klemmt am Ausgang). normY: 0 = oben, 1 = unten. */
    float expressionFromDrag (float startNormY, float currentNormY) const noexcept;

    int cols() const noexcept { return config.cols; }
    int rows() const noexcept { return config.rows; }

    /** Laufzeit-Setter fuer die Sensitivity-Regler (Block A2/A3). Wirkt
        SOFORT auf laufende Drags -- ein Wert, der waehrend eines gehaltenen
        Touches geaendert wird, re-mapt die naechste Bewegung relativ zum
        ursprünglichen Aufsetzpunkt (akzeptierter Nebeneffekt, kein neuer
        Zustand noetig). Aufrufer multipliziert IMMER vom gecachten
        Basiswert aus, nie vom aktuellen Config-Wert (sonst Drift bei
        wiederholtem Setzen). */
    void setYRangeNorm (float newRangeNorm) noexcept;
    void setSemitonesPerPadWidth (float newSemitones) noexcept;

    /** Laufzeit-Setter fuer die In-Tune-Width (Block D1, Settings-Tab).
        Geklemmt auf [0,95] -- siehe pitchBendFromAnchor. */
    void setInTuneWidthPercent (float newPercent) noexcept;

    /** Laufzeit-Setter fuer Oktav-Shift (Block D2, Octave-Up/Down-Buttons).
        Verschiebt lowestNote um newOffsetSemitones (typischerweise
        Vielfache von 12) relativ zur Basis -- der Aufrufer
        (GridKeyboardComponent) haelt die Basis, dieser Setter setzt absolut. */
    void setLowestNote (int newLowestNote) noexcept;
    [[nodiscard]] int lowestNote() const noexcept { return config.lowestNote; }

private:
    Config config;
};

} // namespace conduit::grid
