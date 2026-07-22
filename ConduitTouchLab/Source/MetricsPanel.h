#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

#include "TouchSample.h"

namespace touchlab
{

//==============================================================================
/**
    Live-Kennzahlen je Spur, roh vs. gefiltert:
      - Report-Rate (Hz): Samples/s — zeigt, ob Raw-Pointer mehr Punkte liefert.
      - Jitter im Stillstand (px): Std-Abw. der Position über ein Fenster.
      - Geradheit langsamer Striche (px RMS): Abweichung von der Ausgleichs-
        geraden (Total-Least-Squares) über den laufenden Strich.
    Alles Message-Thread — Allokationen hier unkritisch.
*/
class MetricsPanel final : public juce::Component
{
public:
    enum Lane { NativeRaw = 0, RawRaw, NativeFiltered, RawFiltered, numLanes };

    MetricsPanel();

    void record (Lane lane, const TouchSample& s);
    void resetAll();
    void refresh() { repaint(); }

    void paint (juce::Graphics& g) override;

private:
    struct LaneStats
    {
        // Report-Rate
        std::array<double, 256> tRing {};
        int tWrite = 0;

        // Stillstand-Jitter
        std::array<juce::Point<float>, 48> posRing {};
        int posWrite = 0;
        int posCount = 0;

        // Geradheit (aktueller Strich)
        std::array<juce::Point<float>, 1024> stroke {};
        int strokeCount = 0;

        void record (const TouchSample& s);
        double rateHz (double now) const;
        double jitterPx() const;
        double straightnessRms() const;
    };

    std::array<LaneStats, numLanes> stats;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetricsPanel)
};

} // namespace touchlab
