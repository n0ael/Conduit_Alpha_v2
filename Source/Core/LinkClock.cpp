// Link/asio zuerst — die Netzwerk-Header (WinSock2 etc.) müssen vor allem
// stehen, was windows.h einziehen könnte (JUCE).
#include <ableton/Link.hpp>

#include "LinkClock.h"

namespace conduit
{

struct LinkClock::Impl
{
    explicit Impl (double initialBpm)
        : link (initialBpm)
    {
        link.enableStartStopSync (true);
        link.enable (true);
    }

    ableton::Link link;
};

//==============================================================================
LinkClock::LinkClock (double initialBpm)
    : impl (std::make_unique<Impl> (initialBpm))
{
}

LinkClock::~LinkClock() = default;

void LinkClock::prepare (double sampleRate) noexcept
{
    currentSampleRate.store (sampleRate, std::memory_order_relaxed);
}

//==============================================================================
void LinkClock::setTempo (double bpm)
{
    auto sessionState = impl->link.captureAppSessionState();
    sessionState.setTempo (bpm, impl->link.clock().micros());
    impl->link.commitAppSessionState (sessionState);
}

double LinkClock::getTempo() const
{
    return impl->link.captureAppSessionState().tempo();
}

std::size_t LinkClock::getNumPeers() const
{
    return impl->link.numPeers();
}

//==============================================================================
ClockState LinkClock::captureClockState (int) noexcept
{
    // Lock-free (Link-Garantie für die capture/commitAudioSessionState-API).
    // numSamples bleibt für spätere Output-Latenz-Kompensation reserviert.
    const auto sessionState = impl->link.captureAudioSessionState();
    const auto now = impl->link.clock().micros();

    ClockState state;
    state.bpm              = sessionState.tempo();
    state.beatAtBlockStart = sessionState.beatAtTime (now, quantum);
    state.sampleRate       = currentSampleRate.load (std::memory_order_relaxed);
    state.isPlaying        = sessionState.isPlaying();
    return state;
}

} // namespace conduit
