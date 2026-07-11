#pragma once
#include <vector>
#include <juce_core/juce_core.h>

namespace conduit::grid
{

/** Mehrpunkt-Transferkurve einer Ausdrucksachse. Headless, testbar.
    Geordnete Kontrollpunkte in [0,1]² (mindestens Start (0,0) und Ende
    (1,1)); pro Segment eine stufenlose Krümmung c ∈ [-1,+1] (0 = linear,
    +1 = hart konvex, -1 = hart konkav). apply() formt den Eingang und
    skaliert auf [outputMin, outputMax]. Eingänge außerhalb [0,1] werden
    linear extrapoliert (erhält das Weiterwischen der ungeklemmten Quelle).
    X der Punkte streng aufsteigend (eindeutige Transferfunktion); Y frei. */
class ResponseCurve
{
public:
    struct Point { float x { 0.0f }; float y { 0.0f }; };

    ResponseCurve() noexcept;                    // Default: {0,0},{1,1}, c=0, Range [0,1] -> Identität

    float apply (float input) const noexcept;    // geformter, range-skalierter Ausgang

    // Editier-API (Nutzung durch das spätere Panel, S2):
    void setPoints (const std::vector<Point>& newPoints) noexcept; // >=2, nach X sortiert, X streng aufsteigend erzwungen
    void setSegmentCurvature (int segmentIndex, float c) noexcept;  // c auf [-1,1] geklemmt
    void setOutputRange (float outMin, float outMax) noexcept;

    /** Aktueller Ausgangs-Wertebereich (Panel-Anzeige: Min/Max, Kurven-
        Normalisierung). */
    [[nodiscard]] float getOutputMin() const noexcept { return outputMin; }
    [[nodiscard]] float getOutputMax() const noexcept { return outputMax; }

    int numPoints()   const noexcept;
    int numSegments() const noexcept;                                // = numPoints()-1
    const std::vector<Point>& points() const noexcept;

    /** Krümmung eines Segments (Block-K-Persistenz; 0 bei ungültigem Index). */
    [[nodiscard]] float segmentCurvature (int segmentIndex) const noexcept
    {
        return segmentIndex >= 0 && segmentIndex < (int) curvature.size()
                   ? curvature[(size_t) segmentIndex]
                   : 0.0f;
    }

private:
    std::vector<Point> pts { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
    std::vector<float> curvature { 0.0f };       // je Segment
    float outputMin { 0.0f };
    float outputMax { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseCurve)
};

} // namespace conduit::grid
