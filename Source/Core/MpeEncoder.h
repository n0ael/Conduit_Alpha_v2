#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace conduit::grid
{

//==============================================================================
/**
    Voice-Slot-indizierte Events → MPE-MIDI 1.0 (Lower Zone: Master = Kanal 1,
    Member = Kanäle memberChannelBase .. memberChannelBase+maxVoices-1).

    Zustandslos, allocation-free, kein I/O — der Sink hält die aktive Note
    pro Voice, nicht dieser Encoder. voiceIndex i → Kanal base + i.
*/
class MpeEncoder
{
public:
    struct Config
    {
        int   memberChannelBase       = 2;
        int   maxVoices               = 15;
        float pitchBendRangeSemitones = 48.0f; // MPE-Default pro Member-Kanal
    };

    explicit MpeEncoder (const Config& cfg = {}) noexcept;

    int channelForVoice (int voiceIndex) const noexcept; // 1-basierter MIDI-Kanal

    juce::MidiMessage noteOn    (int voiceIndex, int note, int velocity)        const noexcept;
    juce::MidiMessage noteOff   (int voiceIndex, int note, int releaseVelocity) const noexcept;
    juce::MidiMessage pitchBend (int voiceIndex, float semitones)               const noexcept;
    juce::MidiMessage pressure  (int voiceIndex, float value01)                 const noexcept;
    juce::MidiMessage slide     (int voiceIndex, float value01)                 const noexcept;

private:
    Config config;
};

} // namespace conduit::grid
