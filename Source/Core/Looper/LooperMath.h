#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace conduit::looper
{

//==============================================================================
/**
    Pure Rechenhelfer des Retro-Loopers (Looper-Baustein B1) — bewusst ohne
    JUCE-/Engine-Abhängigkeiten: UI (Segment-Paint des Waveform-Strips),
    LooperEngine (Commit/Phase) und Tests teilen sich exakt dieselbe
    Arithmetik.

    Zeitachsen-Konvention: "Beat" ist der Session-Beat des ClockState
    (Viertelnoten, Link-Achse), ein Takt = quantumBeats (Link-Quantum 4).
    Takt-GRENZE b liegt bei Beat b·quantumBeats — Anker-Semantik wie
    BarSampleAnchors (Grenze b = Ende von Takt b−1 = Anfang von Takt b).

    Segment-Layout des Waveform-Strips (Endlesss-Muster): 4 gleich breite
    Segmente, links → rechts "8 Bars | 4 Bars | 2 Bars | 1 Bar". Rechts
    läuft die Gegenwart ein; jedes Segment zeigt rückwärts ab "jetzt":

        Segment 3 ("1 Bar")  : Offsets [ 0,  4) Beats — der letzte Takt
        Segment 2 ("2 Bars") : Offsets [ 4,  8) — der Takt davor
        Segment 1 ("4 Bars") : Offsets [ 8, 16) — 2 Takte, 2× gestaucht
        Segment 0 ("8 Bars") : Offsets [16, 32) — 4 Takte, 4× gestaucht

    Die Beats-pro-Pixel-Dichte verdoppelt sich an den Grenzen 2→1 und 1→0 —
    die Wellenform staucht sich dort sichtbar und läuft optisch halb so
    schnell weiter. Das Label nennt die GESAMT-Länge bis zur linken Kante
    des Segments: ein Klick committet die letzten label-Takte.
*/

constexpr double quantumBeats = 4.0;  // Link-Quantum: 1 Takt = 4 Beats
constexpr int    numSegments  = 4;
constexpr int    maxBars      = 8;    // Sichtfenster und größter Commit

/** Commit-Länge des Segments in Takten: {8, 4, 2, 1}. 0 außerhalb. */
[[nodiscard]] constexpr int barsForSegment (int segment) noexcept
{
    return segment >= 0 && segment < numSegments ? (maxBars >> segment) : 0;
}

/** Beat-Offset (rückwärts ab "jetzt") der RECHTEN Segment-Kante: {16, 8, 4, 0}.
    Die rechte Kante von Segment s ist die linke Kante des Nachbarn s+1 —
    also dessen Gesamt-Label in Beats. */
[[nodiscard]] constexpr double segmentRightEdgeBeats (int segment) noexcept
{
    return segment >= 0 && segment < numSegments - 1
               ? static_cast<double> (barsForSegment (segment + 1)) * quantumBeats
               : 0.0;
}

/** Beat-Spanne eines Segments: {16, 8, 4, 4} — die linke Kante liegt beim
    eigenen Label, die rechte beim Label des Nachbarn. */
[[nodiscard]] constexpr double segmentSpanBeats (int segment) noexcept
{
    return segment >= 0 && segment < numSegments
               ? static_cast<double> (barsForSegment (segment)) * quantumBeats
                     - segmentRightEdgeBeats (segment)
               : 0.0;
}

/** Segment-Index einer Pixelspalte (0 = links "8 Bars" … 3 = rechts "1 Bar"),
    geclampt auf gültige Segmente; width ≤ 0 fällt aufs rechte Segment. */
[[nodiscard]] inline int segmentForX (double x, double width) noexcept
{
    if (width <= 0.0)
        return numSegments - 1;

    const auto segment = static_cast<int> (std::floor (x * numSegments / width));
    return std::clamp (segment, 0, numSegments - 1);
}

/** Beat-Offset (rückwärts ab "jetzt") einer Pixelspalte — die Kernabbildung
    des gestauchten Scrollens: innerhalb eines Segments linear, an den
    Segment-Grenzen stetig, aber mit springender Dichte (2×).
    x = width ↦ 0 Beats, x = 0 ↦ 32 Beats (8 Takte). */
[[nodiscard]] inline double beatOffsetForX (double x, double width) noexcept
{
    if (width <= 0.0)
        return 0.0;

    const auto segment  = segmentForX (x, width);
    const auto segWidth = width / numSegments;
    const auto fracFromRight = std::clamp (
        (static_cast<double> (segment + 1) * segWidth - x) / segWidth, 0.0, 1.0);

    return segmentRightEdgeBeats (segment) + fracFromRight * segmentSpanBeats (segment);
}

//==============================================================================
/** Bar-aligned Commit-Bereich: die letzten `bars` KOMPLETTEN Takte
    (User-Entscheidung 07/2026 — bewusst kein 1/16-Quantize à la Endlesss:
    die Loop-Grenzen liegen damit per Konstruktion auf dem Link-Taktraster,
    die Playback-Phase ist bar-locked). Grenzen als Takt-Indizes für den
    Anker-Lookup (BarSampleAnchors). */
struct CommitRange
{
    std::int64_t startBar = 0;  // Grenze: Beat startBar·quantumBeats
    std::int64_t endBar   = 0;  // exklusiv — der Loop deckt [startBar, endBar)
    bool valid = false;

    [[nodiscard]] double lengthBeats() const noexcept
    {
        return static_cast<double> (endBar - startBar) * quantumBeats;
    }

    [[nodiscard]] double endBeat() const noexcept
    {
        return static_cast<double> (endBar) * quantumBeats;
    }
};

/** latestBoundaryBar = jüngste sample-genau geankerte Taktgrenze
    (BarSampleAnchors::latestBoundaryBar, −1 = noch keine). Ungültig,
    solange noch keine `bars` kompletten Takte existieren (Session-Anfang). */
[[nodiscard]] inline CommitRange commitRangeForBars (std::int64_t latestBoundaryBar,
                                                     int bars) noexcept
{
    CommitRange range;
    if (bars < 1 || bars > maxBars || latestBoundaryBar < bars)
        return range;

    range.endBar   = latestBoundaryBar;
    range.startBar = latestBoundaryBar - bars;
    range.valid    = true;
    return range;
}

/** Beat-abgeleitete Loop-Phase in [0, loopLengthBeats): das Playback liest
    das Segment [loopEndBeat − L, loopEndBeat) so, dass Session-Beat und
    Loop dauerhaft phasenstarr bleiben — kein freilaufender Zähler, kein
    Drift; Beat-Sprünge (Peer-Join) folgen automatisch. */
[[nodiscard]] inline double loopPhaseBeats (double sessionBeat, double loopEndBeat,
                                            double loopLengthBeats) noexcept
{
    if (loopLengthBeats <= 0.0)
        return 0.0;

    auto phase = std::fmod (sessionBeat - loopEndBeat, loopLengthBeats);
    if (phase < 0.0)
        phase += loopLengthBeats;
    return phase;
}

} // namespace conduit::looper
