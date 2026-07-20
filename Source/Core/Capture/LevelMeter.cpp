#include "LevelMeter.h"

#include <cmath>

namespace conduit
{

//==============================================================================
void LevelMeter::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    activeChannels = juce::jlimit (0, MAX_CAPTURE_CHANNELS, numChannels);

    for (std::size_t ch = 0; ch < meanSquareState.size(); ++ch)
    {
        meanSquareState[ch] = 0.0f;
        peakState[ch]       = 0.0f;
        peakHoldState[ch]   = 0.0f;
        holdRemaining[ch]   = 0.0;
        clipAgeSeconds[ch]  = 0.0;
        primed[ch]          = false;

        peakLevel[ch].store     (0.0f,  std::memory_order_relaxed);
        peakHoldLevel[ch].store (0.0f,  std::memory_order_relaxed);
        rmsLevel[ch].store      (0.0f,  std::memory_order_relaxed);
        clipped[ch].store       (false, std::memory_order_relaxed);
    }
}

//==============================================================================
void LevelMeter::process (const juce::AudioBuffer<float>& buffer, int numChannels) noexcept
{
    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || activeChannels <= 0)
        return;

    const auto channels = juce::jmin (numChannels, buffer.getNumChannels(), activeChannels);

    // Block-Ballistik einmal pro Block — std::exp ist RT-sicher (kein Lock,
    // keine Allokation); die Blocklänge darf zwischen Callbacks variieren.
    // Die Zeitkonstanten kommen aus den klassenweiten Atomics (Metering-Tab)
    // — pro Block gelesen, Änderungen wirken sofort auf alle Instanzen.
    const auto rmsWindowNow   = static_cast<double> (getGlobalRmsWindowSeconds());
    const auto peakReleaseNow = static_cast<double> (getGlobalPeakReleaseSeconds());
    const auto peakHoldNow    = static_cast<double> (getGlobalPeakHoldSeconds());

    const auto blockSeconds = static_cast<double> (numSamples) / sampleRate;
    const auto rmsCoeffBlock = 1.0f - static_cast<float> (std::exp (-1.0 / (rmsWindowNow * sampleRate)));
    const auto peakDecay = static_cast<float> (std::exp (-blockSeconds / peakReleaseNow));
    const auto holdDecay = static_cast<float> (std::exp (-blockSeconds / peakHoldReleaseSeconds));

    for (int ch = 0; ch < channels; ++ch)
        meterOneChannel (ch, buffer.getReadPointer (ch), numSamples,
                         rmsCoeffBlock, peakDecay, holdDecay, peakHoldNow, blockSeconds);
}

void LevelMeter::processPointers (const float* const* channelData, int numChannels,
                                  int numSamples) noexcept
{
    if (numSamples <= 0 || activeChannels <= 0 || channelData == nullptr)
        return;

    const auto channels = juce::jmin (numChannels, activeChannels);

    // Block-Ballistik einmal pro Block — identisch zu process()
    const auto rmsWindowNow   = static_cast<double> (getGlobalRmsWindowSeconds());
    const auto peakReleaseNow = static_cast<double> (getGlobalPeakReleaseSeconds());
    const auto peakHoldNow    = static_cast<double> (getGlobalPeakHoldSeconds());

    const auto blockSeconds = static_cast<double> (numSamples) / sampleRate;
    const auto rmsCoeffBlock = 1.0f - static_cast<float> (std::exp (-1.0 / (rmsWindowNow * sampleRate)));
    const auto peakDecay = static_cast<float> (std::exp (-blockSeconds / peakReleaseNow));
    const auto holdDecay = static_cast<float> (std::exp (-blockSeconds / peakHoldReleaseSeconds));

    for (int ch = 0; ch < channels; ++ch)
    {
        if (channelData[ch] != nullptr)
            meterOneChannel (ch, channelData[ch], numSamples,
                             rmsCoeffBlock, peakDecay, holdDecay, peakHoldNow, blockSeconds);
        else
            meterSilentChannel (ch, numSamples, rmsCoeffBlock, peakDecay, holdDecay,
                                blockSeconds);
    }
}

void LevelMeter::meterSilentChannel (int channel, int numSamples, float rmsCoeffBlock,
                                     float peakDecay, float holdDecay,
                                     double blockSeconds) noexcept
{
    const auto idx = static_cast<std::size_t> (channel);

    // One-Pole mit x = 0 über numSamples Schritte in einem Zug
    if (primed[idx])
        meanSquareState[idx] *= static_cast<float> (
            std::pow (1.0 - static_cast<double> (rmsCoeffBlock), numSamples));

    peakState[idx] *= peakDecay;

    holdRemaining[idx] -= blockSeconds;
    if (holdRemaining[idx] <= 0.0)
        peakHoldState[idx] *= holdDecay;

    // Clip-Latch: Auto-Clear läuft weiter (kein neuer Latch bei Stille)
    if (clipped[idx].load (std::memory_order_relaxed))
    {
        const auto hold = clipHoldSeconds.load (std::memory_order_relaxed);
        if (hold > 0.0f)
        {
            clipAgeSeconds[idx] += blockSeconds;
            if (clipAgeSeconds[idx] >= static_cast<double> (hold))
                clipped[idx].store (false, std::memory_order_relaxed);
        }
    }

    peakLevel[idx].store     (peakState[idx],     std::memory_order_relaxed);
    peakHoldLevel[idx].store (peakHoldState[idx], std::memory_order_relaxed);
    rmsLevel[idx].store      (std::sqrt (meanSquareState[idx]), std::memory_order_relaxed);
}

void LevelMeter::meterOneChannel (int channel, const float* data, int numSamples,
                                  float rmsCoeffBlock, float peakDecay, float holdDecay,
                                  double peakHoldSecondsNow, double blockSeconds) noexcept
{
    const auto idx = static_cast<std::size_t> (channel);

    float meanSquare = meanSquareState[idx];
    float blockPeak  = 0.0f;
    float blockSumSq = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = data[i];
        blockPeak   = juce::jmax (blockPeak, std::abs (x));
        blockSumSq += x * x;
        meanSquare += rmsCoeffBlock * (x * x - meanSquare);
    }

    // Warm-Start pro Kanal: der erste Block seedet mit dem Block-Mittel statt
    // von 0 einzuschwingen
    if (! primed[idx])
    {
        meanSquare = blockSumSq / static_cast<float> (numSamples);
        primed[idx] = true;
    }

    meanSquareState[idx] = meanSquare;
    const float rms = std::sqrt (meanSquare);

    // Peak: sofortiger Attack, Release-Ballistik
    const float peak = juce::jmax (blockPeak, peakState[idx] * peakDecay);
    peakState[idx] = peak;

    // Peak-Hold: neues Maximum hält, sonst nach Ablauf der Haltezeit abfallen
    if (blockPeak >= peakHoldState[idx])
    {
        peakHoldState[idx] = blockPeak;
        holdRemaining[idx] = peakHoldSecondsNow;
    }
    else
    {
        holdRemaining[idx] -= blockSeconds;
        if (holdRemaining[idx] <= 0.0)
            peakHoldState[idx] *= holdDecay;
    }

    // Clip-Latch: bei 0 dBFS setzen; sonst optional nach clipHoldSeconds
    // automatisch verlöschen (0 = nur manueller Reset)
    if (blockPeak >= clipThreshold)
    {
        clipped[idx].store (true, std::memory_order_relaxed);
        clipAgeSeconds[idx] = 0.0;
    }
    else if (clipped[idx].load (std::memory_order_relaxed))
    {
        const auto hold = clipHoldSeconds.load (std::memory_order_relaxed);
        if (hold > 0.0f)
        {
            clipAgeSeconds[idx] += blockSeconds;
            if (clipAgeSeconds[idx] >= static_cast<double> (hold))
                clipped[idx].store (false, std::memory_order_relaxed);
        }
    }

    peakLevel[idx].store     (peak,               std::memory_order_relaxed);
    peakHoldLevel[idx].store (peakHoldState[idx], std::memory_order_relaxed);
    rmsLevel[idx].store      (rms,                std::memory_order_relaxed);
}

//==============================================================================
float LevelMeter::getPeak (int channel) const noexcept
{
    return juce::isPositiveAndBelow (channel, activeChannels)
         ? peakLevel[static_cast<std::size_t> (channel)].load (std::memory_order_relaxed)
         : 0.0f;
}

float LevelMeter::getPeakHold (int channel) const noexcept
{
    return juce::isPositiveAndBelow (channel, activeChannels)
         ? peakHoldLevel[static_cast<std::size_t> (channel)].load (std::memory_order_relaxed)
         : 0.0f;
}

float LevelMeter::getRms (int channel) const noexcept
{
    return juce::isPositiveAndBelow (channel, activeChannels)
         ? rmsLevel[static_cast<std::size_t> (channel)].load (std::memory_order_relaxed)
         : 0.0f;
}

bool LevelMeter::isClipped (int channel) const noexcept
{
    return juce::isPositiveAndBelow (channel, activeChannels)
         && clipped[static_cast<std::size_t> (channel)].load (std::memory_order_relaxed);
}

void LevelMeter::resetClip (int channel) noexcept
{
    if (juce::isPositiveAndBelow (channel, activeChannels))
        clipped[static_cast<std::size_t> (channel)].store (false, std::memory_order_relaxed);
}

void LevelMeter::setClipHoldSeconds (float seconds) noexcept
{
    clipHoldSeconds.store (juce::jmax (0.0f, seconds), std::memory_order_relaxed);
}

} // namespace conduit
