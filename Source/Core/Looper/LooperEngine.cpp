#include "LooperEngine.h"

#include <cmath>

namespace conduit
{

void LooperEngine::prepare (double sampleRate)
{
    preparedSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    crossfadeSamples = juce::jmax (1, (int) std::lround (crossfadeSeconds * preparedSampleRate));
    maxLoopSamples   = (int) std::lround (maxLoopSeconds * preparedSampleRate);

    for (auto& voice : voices)
    {
        // Lead-in + Loop (Klassendoku) — die einzige Allokation der Engine
        voice.buffer.setSize (2, maxLoopSamples + crossfadeSamples);
        voice.buffer.clear();
        voice.gain = 0.0f;
        voice.state.store (VoiceState::free, std::memory_order_release);
    }

    committedBars.store (0, std::memory_order_relaxed);
    stopRequested.store (false, std::memory_order_relaxed);

    // Audio steht (prepareToPlay) — der SampleClock-Reset invalidiert die
    // Beat-Basis, der Playhead snappt im ersten Block neu
    playheadValid = false;
}

//==============================================================================
juce::Result LooperEngine::commit (int bars, const CaptureService& capture,
                                   int leftIndex, int rightIndex,
                                   const BarSampleAnchors& anchors)
{
    if (preparedSampleRate <= 0.0)
        return juce::Result::fail ("Looper nicht vorbereitet");

    if (leftIndex < 0)
        return juce::Result::fail ("Keine Looper-Quelle gewählt");

    const auto range = looper::commitRangeForBars (anchors.latestBoundaryBar(), bars);
    if (! range.valid)
        return juce::Result::fail ("Noch keine " + juce::String (bars)
                                   + " kompletten Takte in der Session");

    std::uint64_t startSample = 0, endSample = 0;
    if (! anchors.lookup (range.startBar, startSample)
        || ! anchors.lookup (range.endBar, endSample)
        || endSample <= startSample)
        return juce::Result::fail ("Taktgrenzen nicht mehr adressierbar");

    const auto numLoopSamples = endSample - startSample;
    if (numLoopSamples > static_cast<std::uint64_t> (maxLoopSamples))
        return juce::Result::fail ("Loop länger als "
                                   + juce::String ((int) maxLoopSeconds) + " s");

    // Freien Voice-Slot beanspruchen (free → filling); bei laufendem Loop
    // ist genau eine Voice aktiv — der zweite Slot ist praktisch immer frei
    Voice* voice = nullptr;
    for (auto& candidate : voices)
    {
        auto expected = VoiceState::free;
        if (candidate.state.compare_exchange_strong (expected, VoiceState::filling,
                                                     std::memory_order_acq_rel))
        {
            voice = &candidate;
            break;
        }
    }

    if (voice == nullptr)
        return juce::Result::fail ("Kein freier Loop-Slot (Commit läuft noch)");

    // Lead-in beginnt crossfadeSamples VOR dem Loop-Start; am Session-
    // Anfang fehlt er ggf. → der unlesbare Teil bleibt Stille
    const auto leadIn = static_cast<std::uint64_t> (crossfadeSamples);
    const auto readStart = startSample >= leadIn ? startSample - leadIn : 0;
    const auto padMissing = static_cast<int> (leadIn - (startSample - readStart));
    const auto totalSamples = static_cast<int> (numLoopSamples) + crossfadeSamples;

    voice->buffer.clear();

    const int indices[] = { leftIndex, rightIndex < 0 ? leftIndex : rightIndex };
    for (int channel = 0; channel < 2; ++channel)
        readChannelChunked (capture.getChannel (indices[channel]), readStart,
                            voice->buffer.getWritePointer (channel) + padMissing,
                            totalSamples - padMissing);

    voice->numLoopSamples = static_cast<int> (numLoopSamples);
    voice->loopEndBeat = range.endBeat();
    voice->lengthBeats = range.lengthBeats();
    voice->samplesPerBeatRecorded = static_cast<double> (numLoopSamples) / range.lengthBeats();

    committedBars.store (bars, std::memory_order_relaxed);

    // Publizieren — der Audio Thread übernimmt im nächsten Block (Fade-In,
    // laufende Voice faded aus)
    voice->state.store (VoiceState::pendingActivate, std::memory_order_release);
    return juce::Result::ok();
}

void LooperEngine::readChannelChunked (const CaptureChannel* channel,
                                       std::uint64_t startPosition,
                                       float* dest, int numSamples)
{
    if (channel == nullptr || numSamples <= 0)
        return;

    // Export-Halte-Protokoll: parallel zum CaptureWriter legal (Zähler);
    // schlägt die Anmeldung fehl (Freigabe läuft), bleibt der Loop Stille
    auto* mutableChannel = const_cast<CaptureChannel*> (channel);
    if (! mutableChannel->tryBeginExportRead())
        return;

    // Chunked: ein Teilbereich mit Loch (Gate war zu) schlägt als Ganzes
    // fehl — kleinere Chunks begrenzen den Stille-Verschnitt an den Rändern
    constexpr int chunkSamples = 65536;
    for (int offset = 0; offset < numSamples; offset += chunkSamples)
    {
        const auto thisChunk = juce::jmin (chunkSamples, numSamples - offset);
        if (! channel->read (startPosition + static_cast<std::uint64_t> (offset),
                             dest + offset, thisChunk))
        {
            // Loch → Stille (buffer ist bereits geleert)
        }
    }

    mutableChannel->endExportRead();
}

//==============================================================================
void LooperEngine::stop() noexcept
{
    stopRequested.store (true, std::memory_order_release);
}

bool LooperEngine::isPlaying() const noexcept
{
    for (const auto& voice : voices)
    {
        const auto state = voice.state.load (std::memory_order_acquire);
        if (state == VoiceState::pendingActivate || state == VoiceState::active)
            return true;
    }

    return false;
}

//==============================================================================
float LooperEngine::renderVoiceSample (const Voice& voice, int channel,
                                       double loopPosition) const noexcept
{
    // Buffer-Layout: [0, F) Lead-in, [F, F+N) Loop — loopPosition ∈ [0, N)
    const auto* data = voice.buffer.getReadPointer (channel);
    const auto fade = crossfadeSamples;
    const auto lastIndex = voice.numLoopSamples + fade - 1;

    const auto readLinear = [&] (double position) noexcept -> float
    {
        const auto clamped = juce::jlimit (0.0, static_cast<double> (lastIndex), position);
        const auto index = static_cast<int> (clamped);
        const auto frac = static_cast<float> (clamped - index);
        const auto next = juce::jmin (index + 1, lastIndex);
        return data[index] + (data[next] - data[index]) * frac;
    };

    const auto zoneStart = static_cast<double> (voice.numLoopSamples - fade);

    // Außerhalb der Wrap-Zone: direkter Loop-Read
    if (loopPosition < zoneStart || voice.numLoopSamples <= fade)
        return readLinear (static_cast<double> (fade) + loopPosition);

    // Wrap-Zone [N−F, N): equal-power vom Loop-Ende auf den Lead-in —
    // bei alpha → 1 landet die Blende exakt auf dem Loop-Start-Sample
    const auto zonePosition = loopPosition - zoneStart;           // [0, F)
    const auto alpha = zonePosition / static_cast<double> (fade); // 0..1

    const auto endSample  = readLinear (static_cast<double> (fade) + loopPosition);
    const auto leadSample = readLinear (zonePosition);            // Lead-in → Loop-Start

    const auto angle = alpha * juce::MathConstants<double>::halfPi;
    return endSample  * static_cast<float> (std::cos (angle))
         + leadSample * static_cast<float> (std::sin (angle));
}

void LooperEngine::process (juce::AudioBuffer<float>& buffer, int numOutputChannels,
                            const ClockState& clock, std::uint64_t blockStartSample,
                            const BarSampleAnchors& anchors) noexcept
{
    // Stop: aktive Voices ausblenden, noch nicht hörbare sofort freigeben
    if (stopRequested.exchange (false, std::memory_order_acq_rel))
    {
        for (auto& voice : voices)
        {
            auto expected = VoiceState::pendingActivate;
            if (voice.state.compare_exchange_strong (expected, VoiceState::free,
                                                     std::memory_order_acq_rel))
                continue;

            expected = VoiceState::active;
            voice.state.compare_exchange_strong (expected, VoiceState::fadingOut,
                                                 std::memory_order_acq_rel);
        }
    }

    // Neuen Commit übernehmen: alte aktive Voice ausblenden, neue einblenden
    for (auto& voice : voices)
    {
        auto expected = VoiceState::pendingActivate;
        if (! voice.state.compare_exchange_strong (expected, VoiceState::active,
                                                   std::memory_order_acq_rel))
            continue;

        voice.gain = 0.0f;
        for (auto& other : voices)
        {
            if (&other == &voice)
                continue;

            expected = VoiceState::active;
            other.state.compare_exchange_strong (expected, VoiceState::fadingOut,
                                                 std::memory_order_acq_rel);
        }
    }

    const auto beatsPerSample = clock.beatsPerSample();
    const auto numSamples = buffer.getNumSamples();
    if (numSamples <= 0 || beatsPerSample <= 0.0)
        return;

    // Beat-Messung jitter-frei aus der SampleClock: Beat des jüngsten
    // Takt-Ankers + Sample-Abstand (Klassendoku). Vor dem ersten Anker
    // bleibt nur der Wall-Clock-Beat — dann existiert aber auch noch
    // kein Loop, der jittern könnte.
    auto measuredBeat = clock.beatAtBlockStart;
    {
        const auto latestBar = anchors.latestBoundaryBar();
        std::uint64_t anchorSample = 0;
        if (latestBar >= 0 && anchors.lookup (latestBar, anchorSample))
            measuredBeat = static_cast<double> (latestBar) * looper::quantumBeats
                         + static_cast<double> (static_cast<std::int64_t> (
                               blockStartSample - anchorSample))
                               * beatsPerSample;
    }

    if (! playheadValid || std::abs (measuredBeat - playheadBeat) > snapThresholdBeats)
    {
        playheadBeat  = measuredBeat;
        playheadValid = true;
    }

    // Slew-limitierte Korrektur: der Lesekopf bleibt sample-kontinuierlich,
    // Mess-Abweichungen (Anker-Rebase, Tempo-Restfehler) werden als
    // unhörbares Varispeed über die nächsten Blöcke abgetragen
    const auto maxCorrection = maxSlewRatio * beatsPerSample
                             * static_cast<double> (numSamples);
    const auto correction = juce::jlimit (-maxCorrection, maxCorrection,
                                          measuredBeat - playheadBeat);
    const auto beatStep = beatsPerSample
                        + correction / static_cast<double> (numSamples);
    const auto blockStartBeat = playheadBeat;
    playheadBeat += beatStep * static_cast<double> (numSamples);

    const auto pair = anchor.load (std::memory_order_relaxed);
    const auto channelA = pair * 2;
    const auto usableChannels = juce::jmin (numOutputChannels, buffer.getNumChannels());
    const auto writable = channelA < usableChannels;

    auto* outA = writable ? buffer.getWritePointer (channelA) : nullptr;
    auto* outB = (writable && channelA + 1 < usableChannels)
               ? buffer.getWritePointer (channelA + 1) : nullptr;

    const auto gainStep = 1.0f / static_cast<float> (juce::jmax (1, crossfadeSamples));

    for (auto& voice : voices)
    {
        const auto state = voice.state.load (std::memory_order_acquire);
        if (state != VoiceState::active && state != VoiceState::fadingOut)
            continue;

        if (voice.numLoopSamples <= 0 || voice.lengthBeats <= 0.0)
        {
            voice.state.store (VoiceState::free, std::memory_order_release);
            continue;
        }

        const auto fading = state == VoiceState::fadingOut;

        for (int i = 0; i < numSamples; ++i)
        {
            // Beat-abgeleitete Phase auf Playhead-Basis: kein Drift,
            // kein Wall-Clock-Jitter, folgt jedem Beat-Sprung
            const auto beat = blockStartBeat + beatStep * i;
            const auto phase = looper::loopPhaseBeats (beat, voice.loopEndBeat,
                                                       voice.lengthBeats);
            const auto position = juce::jmin (
                phase * voice.samplesPerBeatRecorded,
                static_cast<double> (voice.numLoopSamples) - 1.0e-9);

            voice.gain = fading ? juce::jmax (0.0f, voice.gain - gainStep)
                                : juce::jmin (1.0f, voice.gain + gainStep);

            if (voice.gain <= 0.0f && fading)
                break;

            // Anker außerhalb der Kanalzahl: Fades trotzdem nachführen,
            // damit ein Gerätewechsel keine hängenden Voices hinterlässt
            if (outA == nullptr)
                continue;

            outA[i] += renderVoiceSample (voice, 0, position) * voice.gain;
            if (outB != nullptr)
                outB[i] += renderVoiceSample (voice, 1, position) * voice.gain;
        }

        if (fading && voice.gain <= 0.0f)
            voice.state.store (VoiceState::free, std::memory_order_release);
    }
}

} // namespace conduit
