#pragma once

namespace juce { class MidiMessage; }

namespace conduit::grid
{

//==============================================================================
/**
    Schmaler Ausgang für fertige MIDI-Messages (Test-Seam, Muster IOscSink,
    CLAUDE.md 7.3). App: MidiDeviceTarget (juce::MidiOutput). Tests:
    FakeMidiTarget (sammelnd). Aufruf auf dem Message Thread.
*/
class IMidiOutputTarget
{
public:
    virtual ~IMidiOutputTarget() = default;

    virtual void send (const juce::MidiMessage& message) = 0;
};

} // namespace conduit::grid
