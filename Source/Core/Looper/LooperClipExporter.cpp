#include "LooperClipExporter.h"

#include <cstring>

namespace conduit
{

CaptureWriter::Job LooperClipExporter::makeJob (LooperClip& clip,
                                                const juce::String& baseName,
                                                double sampleRate)
{
    CaptureWriter::Job job;
    job.sampleRate = sampleRate;

    // Pin VOR der Publikation des Jobs: Delete/prepare parken den Clip
    // damit im Graveyard, statt ihn unter dem Writer freizugeben
    clip.exportPins.fetch_add (1, std::memory_order_acq_rel);

    auto* clipPointer = &clip;
    const auto contentStart = clip.commitStartSample;
    const auto contentSamples = clip.numContentSamples;
    const auto leadIn = clip.crossfadeSamples;

    const char* suffixes[] = { "_l", "_r" };
    for (int channel = 0; channel < 2; ++channel)
    {
        CaptureWriter::Task task;
        task.trackName = baseName + suffixes[channel];
        task.startPosition = contentStart;
        task.endPosition = contentStart + static_cast<std::uint64_t> (contentSamples);
        task.channelIndex = -1;   // keine Kanal-Folgeaktionen

        // Eingefrorene Quelle (ringCapacitySamples == 0): liest die
        // Content-Region des Clip-Buffers hinter dem Lead-in
        task.source.ringCapacitySamples = 0;
        task.source.read = [clipPointer, channel, contentStart, contentSamples,
                            leadIn] (std::uint64_t position, float* dest,
                                     int numSamples) -> bool
        {
            if (position < contentStart)
                return false;

            const auto offset = static_cast<std::int64_t> (position - contentStart);
            if (offset < 0
                || offset + numSamples > static_cast<std::int64_t> (contentSamples))
                return false;

            const auto* data = clipPointer->buffer.getReadPointer (channel);
            std::memcpy (dest, data + leadIn + offset,
                         sizeof (float) * static_cast<std::size_t> (numSamples));
            return true;
        };

        job.tasks.push_back (std::move (task));
    }

    // Läuft IMMER auf dem Writer-Thread (auch bei Fehlern): Pin lösen —
    // die Freigabe eines Graveyard-Clips wartet darauf
    job.releaseResources = [clipPointer]
    {
        clipPointer->exportPins.fetch_sub (1, std::memory_order_acq_rel);
    };

    return job;
}

juce::Result LooperClipExporter::exportClip (CaptureService& capture, LooperClip& clip,
                                             const juce::String& baseName,
                                             double sampleRate)
{
    if (clip.numContentSamples <= 0)
        return juce::Result::fail ("Clip ist leer");

    auto job = makeJob (clip, baseName, sampleRate);

    if (! capture.enqueueExternalJob (std::move (job)))
    {
        // Job kam nie beim Writer an — Pin sofort wieder lösen
        clip.exportPins.fetch_sub (1, std::memory_order_acq_rel);
        return juce::Result::fail ("Capture-Service nicht bereit");
    }

    return juce::Result::ok();
}

} // namespace conduit
