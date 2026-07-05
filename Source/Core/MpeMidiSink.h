#pragma once

#include <array>

#include <juce_core/juce_core.h>

#include "Interfaces/IMidiOutputTarget.h"
#include "Interfaces/IVoiceSink.h"
#include "MpeEncoder.h"
#include "VoiceAllocator.h" // für kMaxVoices

namespace conduit::grid
{

//==============================================================================
/**
    IVoiceSink → MPE-MIDI 1.0: übersetzt Voice-Events via MpeEncoder in
    juce::MidiMessages und schiebt sie an ein IMidiOutputTarget. Hält pro
    Voice-Slot die aktive Note für korrektes Note-Off. Message Thread.
*/
class MpeMidiSink : public IVoiceSink
{
public:
    MpeMidiSink (IMidiOutputTarget& targetToUse,
                 const MpeEncoder::Config& cfg = {}) noexcept;

    void voiceStart     (int voiceIndex, int note, int velocity) override;
    void voiceStop      (int voiceIndex, int releaseVelocity) override;
    void voicePitchBend (int voiceIndex, float semitones) override;
    void voicePressure  (int voiceIndex, float value) override;
    void voiceSlide     (int voiceIndex, float value) override;
    void allNotesOff    () override;

private:
    IMidiOutputTarget& target;
    MpeEncoder         encoder;
    std::array<int, (size_t) VoiceAllocator::kMaxVoices> activeNote; // -1 = keine

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MpeMidiSink)
};

} // namespace conduit::grid
