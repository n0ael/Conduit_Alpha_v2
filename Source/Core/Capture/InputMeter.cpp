#include "InputMeter.h"

#include <cmath>
#include <limits>

namespace conduit
{

namespace
{
    // Sentinel: Floor noch unbelegt — der erste RMS-Wert schnappt sofort ein
    constexpr float unsetFloor = std::numeric_limits<float>::max();
}

//==============================================================================
void InputMeter::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    activeChannels = juce::jlimit (0, MAX_CAPTURE_CHANNELS, numChannels);

    rmsCoeff = 1.0f - static_cast<float> (std::exp (-1.0 / (rmsWindowSeconds * sampleRate)));
    warmedUp = false;

    for (std::size_t ch = 0; ch < meanSquareState.size(); ++ch)
    {
        meanSquareState[ch] = 0.0f;
        peakState[ch]       = 0.0f;
        floorState[ch]      = unsetFloor;

        peakLevel[ch].store  (0.0f, std::memory_order_relaxed);
        rmsLevel[ch].store   (0.0f, std::memory_order_relaxed);
        noiseFloor[ch].store (0.0f, std::memory_order_relaxed);
    }
}

//==============================================================================
void InputMeter::process (const juce::AudioBuffer<float>& buffer, int numChannels) noexcept
{
    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || activeChannels <= 0)
        return;

    const auto channels = juce::jmin (numChannels, buffer.getNumChannels(), activeChannels);

    // Block-Ballistik einmal pro Block — std::exp ist RT-sicher (kein Lock,
    // keine Allokation), die Blocklänge darf zwischen Callbacks variieren
    const auto blockSeconds = static_cast<double> (numSamples) / sampleRate;
    const auto peakDecay = static_cast<float> (std::exp (-blockSeconds / peakReleaseSeconds));
    const auto floorRise = 1.0f - static_cast<float> (std::exp (-blockSeconds / noiseFloorRiseSeconds));

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto idx = static_cast<std::size_t> (ch);
        const float* data = buffer.getReadPointer (ch);

        float meanSquare = meanSquareState[idx];
        float blockPeak  = 0.0f;
        float blockSumSq = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = data[i];
            blockPeak   = juce::jmax (blockPeak, std::abs (x));
            blockSumSq += x * x;
            meanSquare += rmsCoeff * (x * x - meanSquare);
        }

        // Warm-Start: erster Block seedet mit dem Block-Mittel statt von 0
        // einzuschwingen — der Noise-Floor-Tracker bekommt sofort einen
        // plausiblen Wert und unterschätzt das Grundrauschen nicht dauerhaft
        if (! warmedUp)
            meanSquare = blockSumSq / static_cast<float> (numSamples);

        meanSquareState[idx] = meanSquare;
        const float rms = std::sqrt (meanSquare);

        const float peak = juce::jmax (blockPeak, peakState[idx] * peakDecay);
        peakState[idx] = peak;

        // Minimum-Tracking: abwärts sofort, aufwärts mit ~30-s-Release
        float fl = floorState[idx];
        if (rms < fl)
            fl = rms;
        else
            fl += floorRise * (rms - fl);
        floorState[idx] = fl;

        peakLevel[idx].store  (peak, std::memory_order_relaxed);
        rmsLevel[idx].store   (rms,  std::memory_order_relaxed);
        noiseFloor[idx].store (fl,   std::memory_order_relaxed);
    }

    warmedUp = true;
}

//==============================================================================
float InputMeter::getPeak (int channel) const noexcept
{
    return juce::isPositiveAndBelow (channel, activeChannels)
         ? peakLevel[static_cast<std::size_t> (channel)].load (std::memory_order_relaxed)
         : 0.0f;
}

float InputMeter::getRms (int channel) const noexcept
{
    return juce::isPositiveAndBelow (channel, activeChannels)
         ? rmsLevel[static_cast<std::size_t> (channel)].load (std::memory_order_relaxed)
         : 0.0f;
}

float InputMeter::getNoiseFloor (int channel) const noexcept
{
    return juce::isPositiveAndBelow (channel, activeChannels)
         ? noiseFloor[static_cast<std::size_t> (channel)].load (std::memory_order_relaxed)
         : 0.0f;
}

} // namespace conduit
