#pragma once

#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>

#include "Interfaces/IMidiOutputTarget.h"

namespace conduit::grid
{

//==============================================================================
/**
    IMidiOutputTarget → realer MIDI-Out-Port. Öffnet einen wählbaren,
    EXISTIERENDEN Output (Hardware oder OS-/User-Loopback) via
    openDevice(identifier) — plattformübergreifend. Erzeugt KEINEN eigenen
    virtuellen Port (createNewDevice, plattformabhängig, out of scope).
    Message Thread.
*/
class MidiDeviceTarget : public IMidiOutputTarget
{
public:
    MidiDeviceTarget() = default;

    static juce::Array<juce::MidiDeviceInfo> availableDevices();

    bool         openDevice (const juce::String& identifier);
    void         closeDevice();
    bool         isOpen() const noexcept;
    juce::String currentDeviceIdentifier() const;

    void send (const juce::MidiMessage& message) override;

private:
    std::unique_ptr<juce::MidiOutput> output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiDeviceTarget)
};

} // namespace conduit::grid
