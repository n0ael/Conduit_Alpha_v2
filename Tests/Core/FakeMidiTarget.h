#pragma once

#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Interfaces/IMidiOutputTarget.h"

namespace conduit::grid
{

//==============================================================================
/** Test-Double für IMidiOutputTarget — sammelt jede gesendete Message
    (MpeMidiSinkTests). */
class FakeMidiTarget : public IMidiOutputTarget
{
public:
    std::vector<juce::MidiMessage> messages;

    void send (const juce::MidiMessage& message) override
    {
        messages.push_back (message);
    }
};

} // namespace conduit::grid
