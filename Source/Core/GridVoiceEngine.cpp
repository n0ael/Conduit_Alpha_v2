#include "GridVoiceEngine.h"

namespace conduit::grid
{

GridVoiceEngine::GridVoiceEngine (IVoiceSink& sinkToUse, int maxVoices) noexcept
    : sink (sinkToUse), allocator (maxVoices),
      slideAxis (ExpressionAxis::Config { 0.0f, 1.0f, 1.0f }),
      pitchBendAxis (ExpressionAxis::Config { -kPitchBendRangeSemitones, kPitchBendRangeSemitones, 12.0f })
{
    slotNote.fill (-1);
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
    slotNote[(size_t) voiceIndex] = note;

    pressureAxis.activate (voiceIndex);
    slideAxis.activate (voiceIndex);
    pitchBendAxis.activate (voiceIndex);

    // Ein aktiver Offset gilt sofort auch für frische Noten, nicht erst ab
    // der nächsten Bewegung des Fingers.
    if (! juce::exactlyEqual (pressureAxis.offset(), 0.0f))
        sink.voicePressure (voiceIndex, pressureAxis.combined (voiceIndex));

    if (! juce::exactlyEqual (slideAxis.offset(), 0.0f))
        sink.voiceSlide (voiceIndex, slideAxis.combined (voiceIndex));

    if (! juce::exactlyEqual (pitchBendAxis.offset(), 0.0f))
        sink.voicePitchBend (voiceIndex, pitchBendAxis.combined (voiceIndex));
}

void GridVoiceEngine::noteOff (uint32_t fingerId, int releaseVelocity) noexcept
{
    const int voiceIndex = allocator.release (fingerId);

    if (voiceIndex < 0)
        return;

    sink.voiceStop (voiceIndex, releaseVelocity);
    slotNote[(size_t) voiceIndex] = -1;

    pressureAxis.deactivate (voiceIndex);
    slideAxis.deactivate (voiceIndex);
    pitchBendAxis.deactivate (voiceIndex);
}

void GridVoiceEngine::setPitchBend (uint32_t fingerId, float semitones) noexcept
{
    const int voiceIndex = allocator.voiceForFinger (fingerId);

    if (voiceIndex < 0)
        return;

    pitchBendAxis.setRaw (voiceIndex, semitones);
    sink.voicePitchBend (voiceIndex, pitchBendAxis.combined (voiceIndex));
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

    if (voiceIndex < 0)
        return;

    slideAxis.setRaw (voiceIndex, value01);
    sink.voiceSlide (voiceIndex, slideAxis.combined (voiceIndex));
}

void GridVoiceEngine::allNotesOff() noexcept
{
    sink.allNotesOff();
    allocator.reset();
    pressureAxis.reset();
    slideAxis.reset();
    pitchBendAxis.reset();
    slotNote.fill (-1);
    // Offsets bleiben -- die Ribbon-Stellungen halten über Release-All hinweg.
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

void GridVoiceEngine::setSlideOffset (float bipolarOffset) noexcept
{
    slideAxis.setOffset (bipolarOffset);

    for (int i = 0; i < allocator.maxVoices(); ++i)
    {
        if (slideAxis.isActive (i))
            sink.voiceSlide (i, slideAxis.combined (i));
    }
}

void GridVoiceEngine::setPitchBendOffset (float bipolarOffsetSemitones) noexcept
{
    pitchBendAxis.setOffset (bipolarOffsetSemitones);

    for (int i = 0; i < allocator.maxVoices(); ++i)
    {
        if (pitchBendAxis.isActive (i))
            sink.voicePitchBend (i, pitchBendAxis.combined (i));
    }
}

ExpressionAxis& GridVoiceEngine::axisFor (Axis axis) noexcept
{
    switch (axis)
    {
        case Axis::Pressure:  return pressureAxis;
        case Axis::Slide:     return slideAxis;
        case Axis::PitchBend: return pitchBendAxis;
    }

    return pressureAxis;
}

const ExpressionAxis& GridVoiceEngine::axisFor (Axis axis) const noexcept
{
    return const_cast<GridVoiceEngine&> (*this).axisFor (axis);
}

ResponseCurve& GridVoiceEngine::responseCurve (Axis axis) noexcept
{
    return axisFor (axis).responseCurve();
}

const ResponseCurve& GridVoiceEngine::responseCurve (Axis axis) const noexcept
{
    return axisFor (axis).responseCurve();
}

void GridVoiceEngine::readActiveVoices (Axis axis, std::vector<VoiceReadout>& outVoices) const
{
    const auto& expressionAxis = axisFor (axis);

    outVoices.clear();

    for (int i = 0; i < allocator.maxVoices(); ++i)
    {
        if (expressionAxis.isActive (i))
            outVoices.push_back ({ i, slotNote[(size_t) i], expressionAxis.rawValue (i) });
    }
}

} // namespace conduit::grid
