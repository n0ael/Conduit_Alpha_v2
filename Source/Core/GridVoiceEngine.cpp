#include "GridVoiceEngine.h"

namespace conduit::grid
{

GridVoiceEngine::GridVoiceEngine (IVoiceSink& sinkToUse, int maxVoices) noexcept
    : sink (sinkToUse), allocator (maxVoices)
{
}

void GridVoiceEngine::noteOn (uint32_t fingerId, int note, int velocity) noexcept
{
    uint32_t stolen = 0;
    const int voiceIndex = allocator.allocate (fingerId, stolen);

    if (voiceIndex < 0)
        return;

    if (stolen != 0)
        sink.voiceStop (voiceIndex, 0);

    sink.voiceStart (voiceIndex, note, velocity);
}

void GridVoiceEngine::noteOff (uint32_t fingerId, int releaseVelocity) noexcept
{
    const int voiceIndex = allocator.release (fingerId);

    if (voiceIndex >= 0)
        sink.voiceStop (voiceIndex, releaseVelocity);
}

void GridVoiceEngine::setPitchBend (uint32_t fingerId, float semitones) noexcept
{
    const int voiceIndex = allocator.voiceForFinger (fingerId);

    if (voiceIndex >= 0)
        sink.voicePitchBend (voiceIndex, semitones);
}

void GridVoiceEngine::setPressure (uint32_t fingerId, float value01) noexcept
{
    const int voiceIndex = allocator.voiceForFinger (fingerId);

    if (voiceIndex >= 0)
        sink.voicePressure (voiceIndex, value01);
}

void GridVoiceEngine::setSlide (uint32_t fingerId, float value01) noexcept
{
    const int voiceIndex = allocator.voiceForFinger (fingerId);

    if (voiceIndex >= 0)
        sink.voiceSlide (voiceIndex, value01);
}

void GridVoiceEngine::allNotesOff() noexcept
{
    sink.allNotesOff();
    allocator.reset();
}

} // namespace conduit::grid
