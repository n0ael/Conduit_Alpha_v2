#include "CaptureService.h"

namespace conduit
{

//==============================================================================
void CaptureService::prepare (double sampleRate, int samplesPerBlock, int numInputChannels)
{
    // samplesPerBlock dimensioniert später den PreRoll-Ring (Baustein 3)
    juce::ignoreUnused (samplesPerBlock);

    sampleClock.reset();  // Samplerate-Wechsel invalidiert alle Positionen
    inputMeter.prepare (sampleRate, numInputChannels);
}

//==============================================================================
void CaptureService::processInputTap (const juce::AudioBuffer<float>& buffer,
                                      int numInputChannels) noexcept
{
    // -- Metering: Peak / RMS / Noise-Floor pro Kanal -------------------------
    inputMeter.process (buffer, numInputChannels);

    // -- [Capture-Baustein 2] Gate: Signal-über-Noise-Floor-Detektion ---------
    //    (nutzt inputMeter.getNoiseFloor() als adaptive Schwelle)

    // -- [Capture-Baustein 3] PreRoll-Ring: rohen Input in den Ringbuffer -----
    //    (SPSC, vorallokiert in prepare(); Positionen in SampleClock-Domäne)

    // -- [Capture-Baustein 4] Capture-Trigger / Export -------------------------

    // SampleClock zuletzt: erst wenn alle Bausteine die Samples dieses Blocks
    // verarbeitet haben, wird die neue Position publiziert (release) — Leser,
    // die bis now() konsumieren, sehen garantiert vollständige Daten.
    sampleClock.advance (buffer.getNumSamples());
}

} // namespace conduit
