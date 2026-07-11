#pragma once

#include <functional>
#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>

#include "Util/SpscQueue.h"

namespace conduit::grid
{

//==============================================================================
/** MIDI-NOTEN-Eingang fuer das Pad-Echo (Block H4, User-Wunsch 11.07.2026):
    oeffnet einen existierenden MIDI-In-Port (z. B. "Conduit DAW" -- Lives
    Rueckweg ueber einen Monitor-Track) und pumpt Note-On/Off-Events auf den
    Message Thread. Die Pads der Grid-Page leuchten dann in der
    Fokus-Track-Farbe, wenn die Aufnahme wiedergegeben wird -- OHNE
    Sonne/Mond (reines Feedback).

    Der JUCE-MIDI-Callback laeuft auf einem SYSTEM-Thread -- Uebergabe
    strikt ueber SpscQueue (CLAUDE.md 3.1), ein 60-Hz-Timer entleert sie
    (Muster MidiControlInput). Oeffnet nur existierende Ports. */
class MidiNoteInput final : private juce::MidiInputCallback,
                            private juce::Timer
{
public:
    MidiNoteInput();
    ~MidiNoteInput() override;

    [[nodiscard]] static juce::Array<juce::MidiDeviceInfo> availableDevices();

    bool openDevice (const juce::String& identifier);
    void closeDevice();
    [[nodiscard]] bool isOpen() const noexcept { return input != nullptr; }
    [[nodiscard]] juce::String currentDeviceIdentifier() const;

    /** Note-Events [Message Thread, ~60 Hz gepumpt]. velocity01 in [0,1]. */
    std::function<void (int midiNote, float velocity01)> onNoteOn;
    std::function<void (int midiNote)> onNoteOff;

private:
    struct NoteEvent
    {
        int  note = 0;
        int  velocity = 0;
        bool isOn = false;
    };

    void handleIncomingMidiMessage (juce::MidiInput* source,
                                    const juce::MidiMessage& message) override;
    void timerCallback() override;

    std::unique_ptr<juce::MidiInput> input;
    SpscQueue<NoteEvent> queue { 512 };

    static constexpr int kPumpHz = 60;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiNoteInput)
};

} // namespace conduit::grid
