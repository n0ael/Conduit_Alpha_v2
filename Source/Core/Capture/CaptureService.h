#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "InputMeter.h"
#include "SampleClock.h"

namespace conduit
{

//==============================================================================
/**
    Engine-Service für das Capture-System — Audio-Pendant zu "Capture MIDI".

    Sitzt als Input-Tap VOR dem Graph: processInputTap() sieht den rohen
    Hardware-Input, bevor clockBus/graph/graphFader den Buffer anfassen.
    Graph-Fades und Modul-Outputs gehören nicht in die Aufzeichnung.

    Baustein 1 (dieser Stand): SampleClock + InputMeter als Fundament.
    Spätere Bausteine docken in processInputTap() an — Gate, PreRoll-Ring,
    Capture-Trigger/Export (Marker siehe .cpp).
*/
class CaptureService
{
public:
    CaptureService() = default;

    /** [Message Thread, aus EngineProcessor::prepareToPlay — Audio steht]
        Resettet die SampleClock (Samplerate-Wechsel invalidiert alle
        Positionen) und konfiguriert das Metering. */
    void prepare (double sampleRate, int samplesPerBlock, int numInputChannels);

    /** [Audio Thread] ERSTE Operation in processBlock() — allocation-free,
        lock-free. Misst, taktet und (später) puffert den rohen Input. */
    void processInputTap (const juce::AudioBuffer<float>& buffer, int numInputChannels) noexcept;

    //==========================================================================
    [[nodiscard]] const SampleClock& getSampleClock() const noexcept { return sampleClock; }
    [[nodiscard]] const InputMeter& getInputMeter() const noexcept   { return inputMeter; }

private:
    SampleClock sampleClock;
    InputMeter  inputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureService)
};

} // namespace conduit
