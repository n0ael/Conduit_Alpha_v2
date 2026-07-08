#include "LinkReceiveStream.h"

#include <cmath>
#include <cstring>

namespace conduit
{

namespace
{
    inline float int16ToFloat (std::int16_t value) noexcept
    {
        return static_cast<float> (value) * (1.0f / 32768.0f);
    }

    /** Catmull-Rom zwischen p1 und p2 bei t ∈ [0,1). */
    inline float catmullRom (float p0, float p1, float p2, float p3, float t) noexcept
    {
        const float a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
        const float b = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
        const float c = -0.5f * p0 + 0.5f * p2;
        return ((a * t + b) * t + c) * t + p1;
    }

    /** Beat → Frame-Position innerhalb eines Slots (linear, SDK-Muster). */
    inline double beatToFrame (double beat, double slotBegin, double slotEnd, int numFrames) noexcept
    {
        const double span = slotEnd - slotBegin;
        return span > 0.0 ? (beat - slotBegin) / span * static_cast<double> (numFrames) : 0.0;
    }
}

//==============================================================================
LinkReceiveStream::LinkReceiveStream()
    : ring (std::make_unique<std::array<LinkReceiveSlot, kRingCapacity>>())
{
    static_assert ((kRingCapacity & (kRingCapacity - 1)) == 0,
                   "kRingCapacity ist die Index-Maske — Zweierpotenz nötig");
}

LinkReceiveSlot& LinkReceiveStream::ringAt (int index) noexcept
{
    return (*ring)[static_cast<size_t> ((ringStart + index) & (kRingCapacity - 1))];
}

const LinkReceiveSlot& LinkReceiveStream::ringAt (int index) const noexcept
{
    return (*ring)[static_cast<size_t> ((ringStart + index) & (kRingCapacity - 1))];
}

//==============================================================================
// Link-Thread (Producer)

bool LinkReceiveStream::pushBuffer (const std::int16_t* interleavedSamples,
                                    int numFrames, int numChannels,
                                    double sampleRate, double tempo,
                                    double beatBegin, std::uint64_t count) noexcept
{
    if (interleavedSamples == nullptr || numFrames <= 0 || numChannels <= 0
        || numFrames * numChannels > LinkReceiveSlot::maxSamples
        || sampleRate <= 0.0 || tempo <= 0.0)
    {
        droppedPushes.fetch_add (1, std::memory_order_relaxed);
        return false;
    }

    if (hasLastCount && count != lastCount + 1)
        sequenceGaps.fetch_add (1, std::memory_order_relaxed);
    lastCount    = count;
    hasLastCount = true;

    LinkReceiveSlot slot;
    std::memcpy (slot.samples, interleavedSamples,
                 static_cast<size_t> (numFrames * numChannels) * sizeof (std::int16_t));
    slot.numFrames   = numFrames;
    slot.numChannels = numChannels;
    slot.sampleRate  = sampleRate;
    slot.tempo       = tempo;
    slot.beatBegin   = beatBegin;
    slot.count       = count;

    if (! queue.push (slot))
    {
        droppedPushes.fetch_add (1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

//==============================================================================
// Audio-Thread (Consumer)

void LinkReceiveStream::drainQueueIntoRing() noexcept
{
    // pop() schreibt direkt in den Ziel-Slot am Ring-Ende — keine Zweitkopie.
    // Ring voll → Queue staut sich, der Producer droppt (Zähler); die Front
    // altert gegen targetBegin heraus, der Ring rotiert also weiter.
    while (ringCount < kRingCapacity && queue.pop (ringAt (ringCount)))
        ++ringCount;
}

void LinkReceiveStream::dropFrontSlot() noexcept
{
    ringStart = (ringStart + 1) & (kRingCapacity - 1);
    --ringCount;
}

void LinkReceiveStream::resetRenderState (bool countAsReset) noexcept
{
    startReadPos.reset();
    cursorSlot = 0;
    cursorBase = 0;
    if (countAsReset)
        renderResets.fetch_add (1, std::memory_order_relaxed);
}

void LinkReceiveStream::renderSilence (float* left, float* right,
                                       int numFrames, double sampleRate) noexcept
{
    // Abriss-Declick: letzten Ausgabewert exponentiell ausklingen lassen
    // (~5 ms auf -60 dB), danach echte Stille — Looper-Duck-Lektion.
    const auto decay = static_cast<float> (
        std::exp (std::log (0.001) / (kFadeSeconds * (sampleRate > 0.0 ? sampleRate : 48000.0))));

    for (int i = 0; i < numFrames; ++i)
    {
        lastLeft  *= decay;
        lastRight *= decay;
        left[i]  = lastLeft;
        right[i] = lastRight;
    }

    if (std::abs (lastLeft) < 1.0e-5f)  lastLeft  = 0.0f;
    if (std::abs (lastRight) < 1.0e-5f) lastRight = 0.0f;

    fadeGain = 0.0f;   // nächster Einstieg blendet wieder ein
}

float LinkReceiveStream::sampleAt (std::int64_t frameIndex, int channel) noexcept
{
    if (frameIndex < 0)
        frameIndex = 0;

    // Der Render-Loop fragt idx-1 … idx+2 mit monoton steigendem idx ab;
    // der Cursor folgt idx-1 und rückt nur vor. Ein Rückschritt über die
    // Slot-Grenze (idx+2 → nächstes idx-1) landet wieder im Cursor-Slot.
    if (frameIndex < cursorBase)
    {
        cursorSlot = 0;
        cursorBase = 0;
    }

    while (cursorSlot < ringCount && frameIndex >= cursorBase + ringAt (cursorSlot).numFrames)
    {
        cursorBase += ringAt (cursorSlot).numFrames;
        ++cursorSlot;
    }

    if (cursorSlot >= ringCount)
        return 0.0f;   // hinter dem Ketten-Ende (SDK-Fallback)

    const auto& slot  = ringAt (cursorSlot);
    const auto  local = static_cast<int> (frameIndex - cursorBase);
    const auto  ch    = channel < slot.numChannels ? channel : slot.numChannels - 1;
    return int16ToFloat (slot.samples[local * slot.numChannels + ch]);
}

void LinkReceiveStream::renderBlock (float* left, float* right, int numFrames,
                                     const ClockState& clock, float latencyMs) noexcept
{
    if (left == nullptr || right == nullptr || numFrames <= 0)
        return;

    if (resetRequested.exchange (false, std::memory_order_acq_rel))
    {
        ringStart = 0;
        ringCount = 0;
        resetRenderState (false);
        lastLeft = lastRight = 0.0f;

        // Restbestand der alten Source verwerfen — (*ring)[0] als Scratch,
        // ringCount bleibt 0, der Inhalt wird nie gelesen.
        while (queue.pop ((*ring)[0]))
        {
        }
    }

    drainQueueIntoRing();

    const double bps = clock.beatsPerSample();
    if (bps <= 0.0)
    {
        uiStatus.store (static_cast<int> (Status::idle), std::memory_order_relaxed);
        bufferedSeconds.store (0.0f, std::memory_order_relaxed);
        renderSilence (left, right, numFrames, clock.sampleRate);
        resetRenderState (false);
        return;
    }

    const double latencyBeats = (static_cast<double> (latencyMs) / 1000.0) * (clock.bpm / 60.0);
    const double targetBegin  = clock.beatAtBlockStart - latencyBeats;
    const double targetEnd    = targetBegin + static_cast<double> (numFrames) * bps;

    // Solange nicht gerendert wird: veraltete Slots vorne herausaltern lassen
    if (! startReadPos.has_value())
        while (ringCount > 0 && ringAt (0).endBeat() < targetBegin)
            dropFrontSlot();

    // Vorhandenen Bestand für die UI ausweisen (Tuning-Hilfe für latencyMs)
    auto storeBuffered = [this] (double fromFramePos)
    {
        double seconds = 0.0;
        for (int i = 0; i < ringCount; ++i)
        {
            const auto& s = ringAt (i);
            if (s.sampleRate > 0.0)
                seconds += static_cast<double> (s.numFrames) / s.sampleRate;
        }
        if (ringCount > 0 && ringAt (0).sampleRate > 0.0)
            seconds -= fromFramePos / ringAt (0).sampleRate;
        bufferedSeconds.store (static_cast<float> (seconds > 0.0 ? seconds : 0.0),
                               std::memory_order_relaxed);
    };

    if (ringCount == 0)
    {
        uiStatus.store (static_cast<int> (Status::idle), std::memory_order_relaxed);
        bufferedSeconds.store (0.0f, std::memory_order_relaxed);
        renderSilence (left, right, numFrames, clock.sampleRate);
        resetRenderState (false);
        return;
    }

    if (! startReadPos.has_value() && ringAt (0).beatBegin > targetBegin)
    {
        // Bestand ist noch „zu neu" — das Latenzfenster füllt sich erst
        uiStatus.store (static_cast<int> (Status::waiting), std::memory_order_relaxed);
        storeBuffered (0.0);
        renderSilence (left, right, numFrames, clock.sampleRate);
        return;
    }

    if (! startReadPos.has_value())
    {
        const auto& front = ringAt (0);
        startReadPos = beatToFrame (targetBegin, front.beatBegin, front.endBeat(), front.numFrames);
        cursorSlot   = 0;
        cursorBase   = 0;
        fadeGain     = 0.0f;   // Einstiegs-Declick
    }

    // Frames bis targetEnd über die Slot-Kette einsammeln (SDK-Muster) —
    // deckt zugleich Beat-Sprünge ab: kein Treffer → Reset auf Stille.
    double totalFrames = 0.0;
    bool   foundEnd    = false;

    for (int i = 0; i < ringCount; ++i)
    {
        const auto&  s     = ringAt (i);
        const double begin = s.beatBegin;
        const double end   = s.endBeat();

        if (targetEnd >= begin && targetEnd < end)
        {
            totalFrames += beatToFrame (targetEnd, begin, end, s.numFrames);
            foundEnd = true;
            break;
        }
        totalFrames += static_cast<double> (s.numFrames);
    }

    totalFrames -= startReadPos.value_or (0.0);

    if (! foundEnd || totalFrames <= 0.0)
    {
        uiStatus.store (static_cast<int> (Status::waiting), std::memory_order_relaxed);
        storeBuffered (0.0);
        renderSilence (left, right, numFrames, clock.sampleRate);
        resetRenderState (true);
        return;
    }

    // Re-Pitching: Quell-Frames pro Ausgabe-Frame — handhabt SampleRate-
    // Differenz UND Tempoänderungen (SDK-Referenz LinkAudioRenderer).
    const double frameIncrement = totalFrames / static_cast<double> (numFrames);
    const double readPos        = *startReadPos;

    const auto fadeStep = static_cast<float> (
        1.0 / (kFadeSeconds * (clock.sampleRate > 0.0 ? clock.sampleRate : 48000.0)));

    cursorSlot = 0;
    cursorBase = 0;

    for (int frame = 0; frame < numFrames; ++frame)
    {
        const double framePos = readPos + static_cast<double> (frame) * frameIncrement;
        const double floored  = std::floor (framePos);
        const auto   idx      = static_cast<std::int64_t> (floored);
        const auto   t        = static_cast<float> (framePos - floored);

        const float l = catmullRom (sampleAt (idx - 1, 0), sampleAt (idx, 0),
                                    sampleAt (idx + 1, 0), sampleAt (idx + 2, 0), t);
        const float r = catmullRom (sampleAt (idx - 1, 1), sampleAt (idx, 1),
                                    sampleAt (idx + 1, 1), sampleAt (idx + 2, 1), t);

        if (fadeGain < 1.0f)
            fadeGain = fadeGain + fadeStep < 1.0f ? fadeGain + fadeStep : 1.0f;

        left[frame]  = l * fadeGain;
        right[frame] = r * fadeGain;
    }

    lastLeft  = left[numFrames - 1];
    lastRight = right[numFrames - 1];

    // Leseposition fortschreiben; vollständig konsumierte Front-Slots
    // freigeben — ein Frame Reserve, damit idx-1 des nächsten Blocks
    // erreichbar bleibt.
    double newReadPos = readPos + static_cast<double> (numFrames) * frameIncrement;

    while (ringCount > 1)
    {
        const auto frontFrames = static_cast<double> (ringAt (0).numFrames);
        if (newReadPos - 1.0 >= frontFrames)
        {
            newReadPos -= frontFrames;
            dropFrontSlot();
        }
        else
        {
            break;
        }
    }

    startReadPos = newReadPos;
    storeBuffered (newReadPos);
    uiStatus.store (static_cast<int> (Status::streaming), std::memory_order_relaxed);
}

} // namespace conduit
