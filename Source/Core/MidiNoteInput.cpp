#include "MidiNoteInput.h"

namespace conduit::grid
{

MidiNoteInput::MidiNoteInput()
{
    startTimerHz (kPumpHz);
}

MidiNoteInput::~MidiNoteInput()
{
    stopTimer();
    closeDevice();
}

juce::Array<juce::MidiDeviceInfo> MidiNoteInput::availableDevices()
{
    return juce::MidiInput::getAvailableDevices();
}

bool MidiNoteInput::openDevice (const juce::String& identifier)
{
    closeDevice();

    input = juce::MidiInput::openDevice (identifier, this);
    if (input == nullptr)
        return false;

    input->start();
    return true;
}

void MidiNoteInput::closeDevice()
{
    if (input != nullptr)
    {
        input->stop();
        input.reset();
    }
}

juce::String MidiNoteInput::currentDeviceIdentifier() const
{
    return input != nullptr ? input->getIdentifier() : juce::String();
}

void MidiNoteInput::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    // MIDI-SYSTEM-Thread: nur Noten interessieren, wait-free in die Queue --
    // volle Queue verwirft (das Echo ist reine Anzeige, der 60-Hz-Timer
    // entleert schnell genug). isNoteOff deckt auch NoteOn mit Velocity 0.
    if (message.isNoteOn())
        queue.push ({ message.getNoteNumber(), message.getVelocity(), true });
    else if (message.isNoteOff())
        queue.push ({ message.getNoteNumber(), 0, false });
}

void MidiNoteInput::timerCallback()
{
    NoteEvent event;
    while (queue.pop (event))
    {
        if (event.isOn)
        {
            if (onNoteOn != nullptr)
                onNoteOn (event.note, (float) event.velocity / 127.0f);
        }
        else if (onNoteOff != nullptr)
        {
            onNoteOff (event.note);
        }
    }
}

} // namespace conduit::grid
