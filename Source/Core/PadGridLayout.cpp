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

float PadGridLayout::expressionFromDrag (float startNormY, float currentNormY) const noexcept
{
    return 0.5f + (startNormY - currentNormY) / config.yRangeNorm;
}

} // namespace conduit::grid
