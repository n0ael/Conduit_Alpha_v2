#include "ExpressionAxis.h"

namespace conduit::grid
{

namespace
{
    bool isValidSlot (int voiceIndex) noexcept
    {
        return voiceIndex >= 0 && voiceIndex < VoiceAllocator::kMaxVoices;
    }
}

ExpressionAxis::ExpressionAxis() noexcept : ExpressionAxis (Config{})
{
}

ExpressionAxis::ExpressionAxis (const Config& cfg) noexcept : config (cfg)
{
}

void ExpressionAxis::setRaw (int voiceIndex, float rawValue) noexcept
{
    if (isValidSlot (voiceIndex))
        raw[(size_t) voiceIndex] = rawValue;
}

void ExpressionAxis::setOffset (float bipolarOffset) noexcept
{
    axisOffset = juce::jlimit (-config.offsetScale, config.offsetScale, bipolarOffset);
}

float ExpressionAxis::offset() const noexcept
{
    return axisOffset;
}

void ExpressionAxis::activate (int voiceIndex) noexcept
{
    if (! isValidSlot (voiceIndex))
        return;

    active[(size_t) voiceIndex] = true;
    raw[(size_t) voiceIndex]    = 0.0f;
}

void ExpressionAxis::deactivate (int voiceIndex) noexcept
{
    if (isValidSlot (voiceIndex))
        active[(size_t) voiceIndex] = false;
}

bool ExpressionAxis::isActive (int voiceIndex) const noexcept
{
    return isValidSlot (voiceIndex) && active[(size_t) voiceIndex];
}

float ExpressionAxis::combined (int voiceIndex) const noexcept
{
    if (! isValidSlot (voiceIndex))
        return config.outMin;

    return juce::jlimit (config.outMin, config.outMax, curve.apply (raw[(size_t) voiceIndex]) + axisOffset);
}

void ExpressionAxis::reset() noexcept
{
    raw.fill (0.0f);
    active.fill (false);
}

} // namespace conduit::grid
