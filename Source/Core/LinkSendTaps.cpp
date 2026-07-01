#include "LinkSendTaps.h"

namespace conduit
{

//==============================================================================
// Tap

void LinkSendTaps::Tap::setName (const juce::String& fullChannelName)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (sink != nullptr)
        sink->setName (fullChannelName);  // live zu den Peers (7.2)
}

juce::String LinkSendTaps::Tap::getSinkName() const
{
    JUCE_ASSERT_MESSAGE_THREAD

    return sink != nullptr ? sink->getName() : juce::String();
}

void LinkSendTaps::Tap::setWidth (int newWidth) noexcept
{
    width.store (juce::jlimit (1, 2, newWidth), std::memory_order_relaxed);
}

int LinkSendTaps::Tap::getWidth() const noexcept
{
    return width.load (std::memory_order_relaxed);
}

bool LinkSendTaps::Tap::isActive() const noexcept
{
    return sink != nullptr;
}

std::size_t LinkSendTaps::Tap::getSinkCapacity() const noexcept
{
    return sink != nullptr ? sink->getMaxNumSamples() : 0;
}

void LinkSendTaps::Tap::commit (const float* const* channelData, int numFrames,
                                const ClockState& clock) noexcept
{
    auto* activeSink = rtSink.load();

    if (activeSink == nullptr)
    {
        status.store (static_cast<int> (Status::offline), std::memory_order_relaxed);
        return;
    }

    const int currentWidth = width.load (std::memory_order_relaxed);

    if (numFrames <= 0
        || static_cast<std::size_t> (numFrames) * static_cast<std::size_t> (currentWidth)
               > interleaved.size())
    {
        // Kein Platz (Re-Prepare steht aus): announced bleiben, kein Commit
        // mit abgeschnittenem Buffer.
        status.store (static_cast<int> (Status::announced), std::memory_order_relaxed);
        return;
    }

    convertToInt16Tpdf (channelData, currentWidth, numFrames, interleaved.data(), ditherState);

    const auto result = activeSink->commitFromClockState (interleaved.data(),
                                                          numFrames, currentWidth, clock);

    status.store (static_cast<int> (result == LinkClock::Sink::CommitResult::committed
                                        ? Status::streaming
                                        : Status::announced),
                  std::memory_order_relaxed);
}

void LinkSendTaps::Tap::noteIdle() noexcept
{
    status.store (static_cast<int> (rtSink.load() == nullptr ? Status::offline
                                                             : Status::announced),
                  std::memory_order_relaxed);
}

LinkSendTaps::Status LinkSendTaps::Tap::getStatus() const noexcept
{
    return static_cast<Status> (status.load (std::memory_order_relaxed));
}

//==============================================================================
LinkSendTaps::LinkSendTaps() = default;

LinkSendTaps::~LinkSendTaps()
{
    cancelPendingUpdate();

    // Der Besitzer garantiert, dass kein Audio-Callback mehr läuft (Modul-
    // Destruktor nach Graph-Freigabe bzw. EngineProcessor-Member-Reihenfolge)
    // — die Sinks dürfen direkt destruieren.
    for (auto& tap : taps)
        tap->rtSink.store (nullptr);

    retiredSinks.clear();
    taps.clear();
    disableAudioOnce();  // Preset-Load/Shutdown ohne Phase 1 (5.3)
}

//==============================================================================
void LinkSendTaps::setLinkClock (LinkClock* clock)
{
    JUCE_ASSERT_MESSAGE_THREAD

    linkClock = clock;
}

void LinkSendTaps::prepare (int newSamplesPerBlock)
{
    JUCE_ASSERT_MESSAGE_THREAD

    samplesPerBlock = juce::jmax (1, newSamplesPerBlock);

    const auto capacitySamples = static_cast<std::size_t> (samplesPerBlock) * 2;

    for (auto& tap : taps)
    {
        tap->interleaved.assign (capacitySamples, 0);

        if (tap->sink != nullptr)
            tap->sink->requestMaxNumSamples (capacitySamples);  // wächst-nur (Link-Semantik)
    }
}

LinkSendTaps::Tap* LinkSendTaps::createTap (const juce::String& fullChannelName, int width)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto* clock = linkClock.get();
    if (clock == nullptr)
        return nullptr;  // Tests/Standalone ohne Link-Kontext

    // Inaktiven Pool-Eintrag reaktivieren oder neuen anlegen — Tap-Adressen
    // bleiben über die LinkSendTaps-Lebensdauer stabil.
    Tap* tap = nullptr;
    for (auto& candidate : taps)
        if (candidate->sink == nullptr)
        {
            tap = candidate.get();
            break;
        }

    if (tap == nullptr)
    {
        taps.push_back (std::unique_ptr<Tap> (new Tap()));
        tap = taps.back().get();

        // Eigener, verschiedener Dither-Seed pro Pool-Position (deterministisch)
        tap->ditherState = 0x6c078965u
                           + static_cast<std::uint32_t> (taps.size()) * 2654435761u;
    }

    const auto capacitySamples = static_cast<std::size_t> (samplesPerBlock) * 2;

    // Kapazität immer block × 2 SAMPLES: der Sink trägt mono UND stereo,
    // setWidth() schaltet später ohne Neuanlage um (Namens-/Stream-Stabilität).
    tap->sink = clock->createSink (fullChannelName, capacitySamples);
    tap->interleaved.assign (capacitySamples, 0);
    tap->width.store (juce::jlimit (1, 2, width), std::memory_order_relaxed);
    tap->status.store (static_cast<int> (Status::announced), std::memory_order_relaxed);
    tap->rtSink.store (tap->sink.get());

    ++activeTaps;

    if (! audioEnabled)
    {
        clock->enableAudio (true);  // Refcount: erster aktiver Tap
        audioEnabled = true;
    }

    return tap;
}

void LinkSendTaps::retireTap (Tap* tap)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (tap == nullptr || tap->sink == nullptr)
        return;

    // Phase 1 (5.3): Audio-Thread sofort trennen — Destruktion des Sinks
    // folgt racefrei über den Epoch-Handshake.
    tap->rtSink.store (nullptr);
    tap->status.store (static_cast<int> (Status::offline), std::memory_order_relaxed);

    retiredSinks.push_back (std::move (tap->sink));

    activeTaps = juce::jmax (0, activeTaps - 1);
    if (activeTaps == 0)
        disableAudioOnce();

    retireEpoch = blocksProcessed.load();
    retireDeadlineMs = juce::Time::getMillisecondCounter() + retireGraceMs;
    triggerAsyncUpdate();
}

void LinkSendTaps::retireAll()
{
    JUCE_ASSERT_MESSAGE_THREAD

    for (auto& tap : taps)
        retireTap (tap.get());
}

bool LinkSendTaps::isRetirePending() const noexcept
{
    return ! retiredSinks.empty();
}

void LinkSendTaps::flushPendingRetirement()
{
    JUCE_ASSERT_MESSAGE_THREAD

    handleUpdateNowIfNeeded();
}

int LinkSendTaps::getNumActiveTaps() const noexcept
{
    return activeTaps;
}

//==============================================================================
void LinkSendTaps::noteBlockBegin() noexcept
{
    // Epoch-Handshake: Inkrement VOR den rtSink-Loads der commits (seq_cst)
    blocksProcessed.fetch_add (1);
}

//==============================================================================
void LinkSendTaps::handleAsyncUpdate()
{
    if (retiredSinks.empty())
        return;

    // Self-Re-Dispatch (Muster 5.2 Schritt 3): warten, bis nach dem rtSink-
    // Store ein neuer Audio-Block begonnen hat; sonst destruiert die Deadline.
    if (blocksProcessed.load() == retireEpoch
        && juce::Time::getMillisecondCounter() < retireDeadlineMs)
    {
        triggerAsyncUpdate();
        return;
    }

    retiredSinks.clear();  // Kanäle verschwinden jetzt bei den Peers
}

void LinkSendTaps::disableAudioOnce()
{
    if (! audioEnabled)
        return;

    audioEnabled = false;

    if (auto* clock = linkClock.get())
        clock->enableAudio (false);  // Refcount: letzter aktiver Tap
}

//==============================================================================
void LinkSendTaps::convertToInt16Tpdf (const float* const* channelData,
                                       int numChannels, int numFrames,
                                       std::int16_t* dest,
                                       std::uint32_t& ditherState) noexcept
{
    for (int frame = 0; frame < numFrames; ++frame)
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            // TPDF = Differenz zweier Uniform-Werte aus dem LCG (3.1):
            // Dreieck über ±1 LSB, Mittelwert 0 — kein rand(), kein Heap.
            ditherState = ditherState * 1664525u + 1013904223u;
            const auto r1 = static_cast<float> (ditherState >> 8) * (1.0f / 16777216.0f);
            ditherState = ditherState * 1664525u + 1013904223u;
            const auto r2 = static_cast<float> (ditherState >> 8) * (1.0f / 16777216.0f);

            const auto dithered = channelData[channel][frame] * 32767.0f + (r1 - r2);

            *dest++ = static_cast<std::int16_t> (
                juce::roundToInt (juce::jlimit (-32768.0f, 32767.0f, dithered)));
        }
    }
}

} // namespace conduit
