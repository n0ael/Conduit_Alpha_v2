#pragma once

#include <vector>

#include "Interfaces/IVoiceSink.h"

namespace conduit::grid
{

//==============================================================================
/** Test-Double für IVoiceSink — protokolliert jeden Aufruf statt ihn
    umzusetzen (GridVoiceEngineTests). */
class FakeVoiceSink : public IVoiceSink
{
public:
    enum class Kind { VoiceStart, VoiceStop, PitchBend, Pressure, Slide, AllNotesOff };

    struct Call
    {
        Kind  kind;
        int   voiceIndex  = -1;
        int   intValue    = 0;    // Start: note   | Stop: releaseVelocity
        int   intValue2   = 0;    // Start: velocity
        float floatValue  = 0.0f; // PitchBend/Pressure/Slide
    };

    std::vector<Call> calls;

    void voiceStart (int voiceIndex, int note, int velocity) override
    {
        calls.push_back ({ Kind::VoiceStart, voiceIndex, note, velocity, 0.0f });
    }

    void voiceStop (int voiceIndex, int releaseVelocity) override
    {
        calls.push_back ({ Kind::VoiceStop, voiceIndex, releaseVelocity, 0, 0.0f });
    }

    void voicePitchBend (int voiceIndex, float semitones) override
    {
        calls.push_back ({ Kind::PitchBend, voiceIndex, 0, 0, semitones });
    }

    void voicePressure (int voiceIndex, float value) override
    {
        calls.push_back ({ Kind::Pressure, voiceIndex, 0, 0, value });
    }

    void voiceSlide (int voiceIndex, float value) override
    {
        calls.push_back ({ Kind::Slide, voiceIndex, 0, 0, value });
    }

    void allNotesOff() override
    {
        calls.push_back ({ Kind::AllNotesOff, -1, 0, 0, 0.0f });
    }
};

} // namespace conduit::grid
