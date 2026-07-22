#include "MetricsPanel.h"

#include <cmath>

namespace touchlab
{

//==============================================================================
void MetricsPanel::LaneStats::record (const TouchSample& s)
{
    tRing[(size_t) tWrite] = s.tSeconds;
    tWrite = (tWrite + 1) % (int) tRing.size();

    posRing[(size_t) posWrite] = { s.x, s.y };
    posWrite = (posWrite + 1) % (int) posRing.size();
    posCount = juce::jmin (posCount + 1, (int) posRing.size());

    if (s.phase == Phase::Down)
        strokeCount = 0;

    if (strokeCount < (int) stroke.size())
        stroke[(size_t) strokeCount++] = { s.x, s.y };
}

double MetricsPanel::LaneStats::rateHz (double now) const
{
    int count = 0;
    for (double t : tRing)
        if (t > 0.0 && now - t <= 1.0)
            ++count;
    return (double) count;
}

double MetricsPanel::LaneStats::jitterPx() const
{
    if (posCount < 2)
        return 0.0;

    juce::Point<double> mean { 0.0, 0.0 };
    for (int i = 0; i < posCount; ++i)
        mean += posRing[(size_t) i].toDouble();
    mean /= (double) posCount;

    double var = 0.0;
    for (int i = 0; i < posCount; ++i)
    {
        const auto d = posRing[(size_t) i].toDouble() - mean;
        var += d.x * d.x + d.y * d.y;
    }
    return std::sqrt (var / (double) posCount); // sqrt(Var_x + Var_y)
}

double MetricsPanel::LaneStats::straightnessRms() const
{
    if (strokeCount < 8)
        return 0.0;

    juce::Point<double> mean { 0.0, 0.0 };
    for (int i = 0; i < strokeCount; ++i)
        mean += stroke[(size_t) i].toDouble();
    mean /= (double) strokeCount;

    double sxx = 0.0, syy = 0.0, sxy = 0.0;
    for (int i = 0; i < strokeCount; ++i)
    {
        const auto d = stroke[(size_t) i].toDouble() - mean;
        sxx += d.x * d.x;
        syy += d.y * d.y;
        sxy += d.x * d.y;
    }
    const double n = (double) strokeCount;
    sxx /= n; syy /= n; sxy /= n;

    // Kleinerer Eigenwert der Kovarianz = Varianz quer zur Ausgleichsgeraden.
    const double tr = sxx + syy;
    const double det = sxx * syy - sxy * sxy;
    const double disc = std::sqrt (juce::jmax (0.0, tr * tr * 0.25 - det));
    const double lambdaMin = tr * 0.5 - disc;
    return std::sqrt (juce::jmax (0.0, lambdaMin));
}

//==============================================================================
MetricsPanel::MetricsPanel() {}

void MetricsPanel::record (Lane lane, const TouchSample& s)
{
    stats[(size_t) lane].record (s);
}

void MetricsPanel::resetAll()
{
    stats = {};
    repaint();
}

//==============================================================================
void MetricsPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1c1f));

    auto area = getLocalBounds().reduced (8);
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (14.0f);
    g.drawText ("Messwerte (roh vs. gefiltert)", area.removeFromTop (20),
                juce::Justification::topLeft);
    area.removeFromTop (4);

    const int cLane = 150, cRate = 90, cJit = 110;
    auto header = area.removeFromTop (18);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (12.0f);
    g.drawText ("Spur",           header.removeFromLeft (cLane), juce::Justification::left);
    g.drawText ("Rate Hz",        header.removeFromLeft (cRate), juce::Justification::left);
    g.drawText (juce::String::fromUTF8 ("Jitter σ px"),
                header.removeFromLeft (cJit),  juce::Justification::left);
    g.drawText ("Gerade RMS px",  header,                        juce::Justification::left);

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const char* names[numLanes] = { "Nativ roh", "Raw-Ptr roh", "Nativ gefilt.", "Raw-Ptr gefilt." };
    const juce::Colour rowCols[numLanes] = {
        juce::Colour (0xff42d0ff), juce::Colour (0xffffa53d),
        juce::Colours::white,      juce::Colour (0xff7ee081)
    };

    g.setFont (13.0f);
    for (int i = 0; i < numLanes; ++i)
    {
        auto row = area.removeFromTop (20);
        const auto& st = stats[(size_t) i];

        g.setColour (rowCols[i]);
        g.drawText (names[i], row.removeFromLeft (cLane), juce::Justification::left);

        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawText (juce::String (st.rateHz (now), 0),
                    row.removeFromLeft (cRate), juce::Justification::left);
        g.drawText (juce::String (st.jitterPx(), 2),
                    row.removeFromLeft (cJit), juce::Justification::left);
        g.drawText (juce::String (st.straightnessRms(), 2),
                    row, juce::Justification::left);
    }
}

} // namespace touchlab
