#include "PadGridLayout.h"

#include <cmath>

namespace conduit::grid
{

PadGridLayout::PadGridLayout() noexcept : PadGridLayout (Config{})
{
}

PadGridLayout::PadGridLayout (const Config& cfg) noexcept : config (cfg)
{
}

int PadGridLayout::padIndexAt (float normX, float normY) const noexcept
{
    const auto col = (int) std::floor (normX * (float) config.cols);
    const auto row = (int) std::floor (normY * (float) config.rows);

    if (col < 0 || col >= config.cols || row < 0 || row >= config.rows)
        return -1;

    return row * config.cols + col;
}

int PadGridLayout::noteForPad (int padIndex) const noexcept
{
    const auto col          = padIndex % config.cols;
    const auto rowFromTop   = padIndex / config.cols;
    const auto rowFromBottom = (config.rows - 1) - rowFromTop;

    return config.lowestNote + col + rowFromBottom * config.semitonesPerRow;
}

int PadGridLayout::noteAt (float normX, float normY) const noexcept
{
    const auto pad = padIndexAt (normX, normY);
    return pad < 0 ? -1 : noteForPad (pad);
}

float PadGridLayout::pitchBendSemitones (float startNormX, float currentNormX) const noexcept
{
    const auto padWidth = 1.0f / (float) config.cols;

    // NICHT geklemmt -- die pitchBendAxis/der Encoder klemmen erst am
    // Ausgang (CLAUDE.md 14 ADR).
    return (currentNormX - startNormX) / padWidth * config.semitonesPerPadWidth;
}

float PadGridLayout::pitchBendFromAnchor (float anchorNormX, float currentNormX) const noexcept
{
    const auto padWidth = 1.0f / (float) config.cols;
    const auto distancePads = (currentNormX - anchorNormX) / padWidth;   // in Pad-Breiten

    // In-Tune-Zonenbreite in Pad-Breiten, geklemmt < 1 (Zone darf das Pad
    // nicht komplett fuellen, sonst gaebe es keinen Bend-Weg mehr).
    const auto zoneWidth = juce::jlimit (0.0f, 0.95f, config.inTuneWidthPercent / 100.0f);
    const auto halfZone  = zoneWidth * 0.5f;

    // Treppen-Kennlinie: In-Tune-Zonen um jeden ganzzahligen Pad-Abstand
    // (0, +-1, +-2, ...); zwischen zwei Zonen laeuft der Bend linear von
    // n*k nach (n+1)*k -- der naechste Pad-Mittelpunkt liegt also exakt bei
    // +-semitonesPerPadWidth (stetig, monoton, ungeklemmt).
    const auto magnitude = std::abs (distancePads);
    const auto nearest   = std::round (magnitude);
    const auto fromZone  = magnitude - nearest;   // [-0.5, 0.5] um das naechste Raster

    float steps;
    if (std::abs (fromZone) <= halfZone)
        steps = nearest;                          // innerhalb der In-Tune-Zone
    else if (fromZone > 0.0f)
        steps = nearest + (fromZone - halfZone) / (1.0f - zoneWidth);
    else
        steps = nearest + (fromZone + halfZone) / (1.0f - zoneWidth);

    const auto sign = distancePads < 0.0f ? -1.0f : 1.0f;
    return sign * steps * config.semitonesPerPadWidth;
}

float PadGridLayout::padCentreNormX (int padIndex) const noexcept
{
    const auto col = padIndex % config.cols;
    return ((float) col + 0.5f) / (float) config.cols;
}

float PadGridLayout::expressionFromDrag (float startNormY, float currentNormY) const noexcept
{
    return 0.5f + (startNormY - currentNormY) / config.yRangeNorm;
}

void PadGridLayout::setYRangeNorm (float newRangeNorm) noexcept
{
    config.yRangeNorm = juce::jmax (0.01f, newRangeNorm);
}

void PadGridLayout::setSemitonesPerPadWidth (float newSemitones) noexcept
{
    config.semitonesPerPadWidth = juce::jmax (0.01f, newSemitones);
}

void PadGridLayout::setInTuneWidthPercent (float newPercent) noexcept
{
    config.inTuneWidthPercent = juce::jlimit (0.0f, 95.0f, newPercent);
}

void PadGridLayout::setLowestNote (int newLowestNote) noexcept
{
    config.lowestNote = juce::jlimit (0, 127, newLowestNote);
}

} // namespace conduit::grid
