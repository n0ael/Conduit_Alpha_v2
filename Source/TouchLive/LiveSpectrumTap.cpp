#include "LiveSpectrumTap.h"

#include <cmath>

namespace conduit
{

//==============================================================================
LiveSpectrumTap::LiveSpectrumTap (LinkClock* clockToUse)
    : linkClock (clockToUse)
{
    magnitudesDb.fill (-90.0f);

    if (linkClock != nullptr)
        linkClock->addChangeListener (this);
}

LiveSpectrumTap::~LiveSpectrumTap()
{
    stopTimer();

    if (linkClock != nullptr)
        linkClock->removeChangeListener (this);

    inputTapEnabled.store (false, std::memory_order_release);
    linkSource.reset();
}

//==============================================================================
void LiveSpectrumTap::setMode (SourceMode newMode)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (mode == newMode)
        return;

    // Erst die alte Quelle stoppen (SPSC: nie zwei Producer), dann leeren
    inputTapEnabled.store (false, std::memory_order_release);
    linkSource.reset();
    sourceLabel.clear();
    mode = newMode;

    drainQueueInto (true);
    ring.fill (0.0f);
    ringWrite = 0;
    ringFill = 0;
    samplesSinceAnalysis = 0;
    magnitudesDb.fill (-90.0f);
    ++revision;

    if (mode == SourceMode::linkAudio)
    {
        bindFirstAvailableChannel();
    }
    else if (mode == SourceMode::audioInput)
    {
        sourceLabel = "Input";
        inputTapEnabled.store (true, std::memory_order_release);
    }

    if (mode == SourceMode::off)
        stopTimer();
    else
        startTimerHz (30);
}

void LiveSpectrumTap::setAveraging (double amount01)
{
    averaging = juce::jlimit (0.0, 1.0, amount01);
}

double LiveSpectrumTap::binToHz (int bin) const noexcept
{
    return analysisSampleRate * bin / fftSize;
}

bool LiveSpectrumTap::isReceiving() const noexcept
{
    return mode != SourceMode::off
        && juce::Time::getMillisecondCounter() - lastDataMs < 600;
}

juce::String LiveSpectrumTap::getSourceLabel() const
{
    return sourceLabel;
}

//==============================================================================
void LiveSpectrumTap::bindFirstAvailableChannel()
{
    if (linkClock == nullptr)
        return;

    const auto channels = linkClock->availableChannels();

    if (channels.empty())
    {
        sourceLabel = juce::String::fromUTF8 ("kein Link-Kanal…");
        linkSource.reset();
        return;
    }

    const auto& channel = channels.front();

    if (linkSource != nullptr && linkSource->channelId() == channel.id)
        return;   // schon gebunden

    sourceLabel = channel.peerName + juce::String::fromUTF8 (" \xC2\xB7 ")
                + channel.name;

    // Callback [Link-Thread]: int16→float mono, Chunks in die Queue.
    // Fürs Anzeige-Spektrum ist der Link-Thread nicht RT-kritisch.
    linkSource = linkClock->createSource (channel.id,
        [this] (const LinkClock::Source::ReceivedBuffer& buffer)
        {
            constexpr float toFloat = 1.0f / 32768.0f;
            const auto numChannels = juce::jmax (1, buffer.numChannels);

            int frame = 0;

            while (frame < buffer.numFrames)
            {
                Chunk chunk;
                chunk.sampleRate = (float) buffer.sampleRate;
                chunk.numSamples = juce::jmin (Chunk::maxSamples,
                                               buffer.numFrames - frame);

                for (int i = 0; i < chunk.numSamples; ++i)
                {
                    const auto* framePtr = buffer.interleavedSamples
                                         + (frame + i) * numChannels;
                    float sum = 0.0f;

                    for (int ch = 0; ch < numChannels; ++ch)
                        sum += (float) framePtr[ch];

                    chunk.samples[i] = sum * toFloat / (float) numChannels;
                }

                if (! queue.push (chunk))
                    break;   // voll → Rest droppen (nur Anzeige)

                frame += chunk.numSamples;
            }
        });
}

void LiveSpectrumTap::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // ChannelsChanged: im Link-Modus ggf. (neu) binden
    if (mode == SourceMode::linkAudio)
        bindFirstAvailableChannel();
}

//==============================================================================
void LiveSpectrumTap::pushAudioBlock (const float* const* channels, int numChannels,
                                      int numSamples) noexcept
{
    if (! inputTapEnabled.load (std::memory_order_acquire)
        || numChannels < 1 || numSamples <= 0)
        return;

    const auto* left = channels[0];
    const auto* right = numChannels > 1 ? channels[1] : nullptr;
    const auto sampleRate = audioSampleRate.load (std::memory_order_relaxed);

    int offset = 0;

    while (offset < numSamples)
    {
        Chunk chunk;
        chunk.sampleRate = sampleRate;
        chunk.numSamples = juce::jmin (Chunk::maxSamples, numSamples - offset);

        for (int i = 0; i < chunk.numSamples; ++i)
        {
            const auto index = offset + i;
            chunk.samples[i] = right != nullptr
                                   ? 0.5f * (left[index] + right[index])
                                   : left[index];
        }

        if (! queue.push (chunk))
            return;   // voll → Drop (nur Anzeige)

        offset += chunk.numSamples;
    }
}

void LiveSpectrumTap::setAudioSampleRate (double newSampleRate) noexcept
{
    if (newSampleRate > 0.0)
        audioSampleRate.store ((float) newSampleRate, std::memory_order_relaxed);
}

//==============================================================================
void LiveSpectrumTap::drainQueueInto (bool discard)
{
    Chunk chunk;

    while (queue.pop (chunk))
    {
        if (discard)
            continue;

        analysisSampleRate = chunk.sampleRate;
        lastDataMs = juce::Time::getMillisecondCounter();

        // Echter Ringpuffer (Index-Maske) — analyseNow linearisiert
        for (int i = 0; i < chunk.numSamples; ++i)
        {
            ring[(size_t) ringWrite] = chunk.samples[i];
            ringWrite = (ringWrite + 1) & (fftSize - 1);
        }

        ringFill = juce::jmin (fftSize, ringFill + chunk.numSamples);
        samplesSinceAnalysis += chunk.numSamples;
    }
}

void LiveSpectrumTap::analyseNow()
{
    JUCE_ASSERT_MESSAGE_THREAD

    drainQueueInto (false);

    if (ringFill < fftSize || samplesSinceAnalysis == 0)
        return;

    samplesSinceAnalysis = 0;

    // Linearisieren: ältestes Sample liegt bei ringWrite
    const auto firstPart = fftSize - ringWrite;
    std::copy_n (ring.data() + ringWrite, firstPart, fftScratch.begin());
    std::copy_n (ring.data(), ringWrite, fftScratch.begin() + firstPart);
    std::fill (fftScratch.begin() + fftSize, fftScratch.end(), 0.0f);
    window.multiplyWithWindowingTable (fftScratch.data(), fftSize);
    fft.performFrequencyOnlyForwardTransform (fftScratch.data());

    // Normierung: Vollaussteuerungs-Sinus ≈ 0 dBFS (JUCE-FFT-Magnitude
    // × Hann-Kohärenzgewinn, empirisch im Unit-Test verifiziert)
    const auto scale = 2.0f / (float) fftSize;
    const auto smoothing = (float) std::pow (averaging, 0.35);

    for (int bin = 0; bin < numBins; ++bin)
    {
        const auto magnitude = fftScratch[(size_t) bin] * scale;
        const auto db = juce::jlimit (-90.0f, 12.0f,
                                      juce::Decibels::gainToDecibels (magnitude,
                                                                      -90.0f));
        auto& slot = magnitudesDb[(size_t) bin];
        slot = smoothing * slot + (1.0f - smoothing) * db;
    }

    ++revision;
}

void LiveSpectrumTap::timerCallback()
{
    analyseNow();
}

} // namespace conduit
