#include "MidiDeviceTarget.h"

namespace conduit::grid
{

juce::Array<juce::MidiDeviceInfo> MidiDeviceTarget::availableDevices()
{
    return juce::MidiOutput::getAvailableDevices();
}

bool MidiDeviceTarget::openDevice (const juce::String& identifier)
{
    output = juce::MidiOutput::openDevice (identifier);
    return output != nullptr;
}

void MidiDeviceTarget::closeDevice()
{
    output.reset();
}

bool MidiDeviceTarget::isOpen() const noexcept
{
    return output != nullptr;
}

juce::String MidiDeviceTarget::currentDeviceIdentifier() const
{
    return output != nullptr ? output->getIdentifier() : juce::String();
}

void MidiDeviceTarget::send (const juce::MidiMessage& message)
{
    if (output != nullptr)
        output->sendMessageNow (message);
}

} // namespace conduit::grid
