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
    pressureAxis.activate (voiceIndex);

    // Ein aktiver Offset gilt sofort auch für frische Noten, nicht erst ab
    // der nächsten Y-Bewegung des Fingers.
    if (! juce::exactlyEqual (pressureAxis.offset(), 0.0f))
        sink.voicePressure (voiceIndex, pressureAxis.combined (voiceIndex));
}

void GridVoiceEngine::noteOff (uint32_t fingerId, int releaseVelocity) noexcept
{
    const int voiceIndex = allocator.release (fingerId);

    if (voiceIndex < 0)
        return;

    sink.voiceStop (voiceIndex, releaseVelocity);
    pressureAxis.deactivate (voiceIndex);
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

    if (voiceIndex < 0)
        return;

    pressureAxis.setRaw (voiceIndex, value01);
    sink.voicePressure (voiceIndex, pressureAxis.combined (voiceIndex));
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
    pressureAxis.reset();
    // Offset bleibt -- die Ribbon-Stellung hält über Release-All hinweg.
}

void GridVoiceEngine::setGlobalVolume (float value01) noexcept
{
    sink.setGlobalVolume (value01);
}

void GridVoiceEngine::setPressureOffset (float bipolarOffset) noexcept
{
    pressureAxis.setOffset (bipolarOffset);

    for (int i = 0; i < allocator.maxVoices(); ++i)
    {
        if (pressureAxis.isActive (i))
            sink.voicePressure (i, pressureAxis.combined (i));
    }
}

} // namespace conduit::grid
