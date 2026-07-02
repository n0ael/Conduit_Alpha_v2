#include "Metronome.h"

#include <cmath>

namespace conduit
{

void Metronome::prepare (double sampleRate) noexcept
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    envelopeCoeff = (float) std::exp (-1.0 / (decaySeconds * currentSampleRate));
    envelope  = 0.0f;
    phase     = 0.0;
    beatValid = false;
}

void Metronome::process (juce::AudioBuffer<float>& buffer, int numOutputChannels,
                         const ClockState& clock) noexcept
{
    const auto isEnabled = enabled.load (std::memory_order_relaxed);

    const auto pair = anchor.load (std::memory_order_relaxed);
    const auto channelA = pair * 2;
    const auto channelB = channelA + 1;
    const auto numSamples = buffer.getNumSamples();
    const auto usableChannels = juce::jmin (numOutputChannels, buffer.getNumChannels());

    if (channelA >= usableChannels)
    {
        // Anker außerhalb der Kanalzahl — Zustand trotzdem nachführen,
        // damit ein Gerätewechsel keine aufgestauten Trigger feuert
        previousBeat = clock.beatAtBlockStart + clock.beatsPerSample() * numSamples;
        beatValid = isEnabled;
        return;
    }

    auto* outA = buffer.getWritePointer (channelA);
    auto* outB = channelB < usableChannels ? buffer.getWritePointer (channelB) : nullptr;

    const auto beatsPerSample = clock.beatsPerSample();
    auto beat = clock.beatAtBlockStart;

    // Sprung in der Beat-Achse (Offset-Änderung, Transport, erster Block):
    // nicht rückwirkend triggern
    if (! beatValid || std::abs (beat - previousBeat) > 1.0)
        previousBeat = beat;

    beatValid = true;

    for (int i = 0; i < numSamples; ++i)
    {
        beat = clock.beatAtBlockStart + beatsPerSample * i;

        // Beat-Grenze überquert (Muster 4.5) — nur mit aktivem Metronom
        if (isEnabled && std::floor (beat) > std::floor (previousBeat))
        {
            const auto beatIndex = (std::int64_t) std::floor (beat);
            const auto downbeat  = (beatIndex % 4 + 4) % 4 == 0;

            envelope = 1.0f;
            phase    = 0.0;
            phaseInc = juce::MathConstants<double>::twoPi
                       * (downbeat ? downbeatFreqHz : beatFreqHz) / currentSampleRate;
        }

        previousBeat = beat;

        if (envelope > silentLevel)
        {
            // Cosinus: Onset auf dem Maximum — perkussiver Click, und der
            // Trigger-Sample selbst ist nie 0 (sample-genau messbar)
            const auto sample = (float) std::cos (phase) * envelope * clickGain;
            phase    += phaseInc;
            envelope *= envelopeCoeff;

            outA[i] += sample;

            if (outB != nullptr)
                outB[i] += sample;
        }
        else if (envelope != 0.0f)
        {
            envelope = 0.0f;  // ausgeklungen — kein Denormal-Grundrauschen
        }
    }
}

} // namespace conduit
