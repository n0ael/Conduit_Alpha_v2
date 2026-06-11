#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

#include "Interfaces/IClockSource.h"

namespace conduit
{

//==============================================================================
/**
    Clock-Master auf Basis einer Ableton-Link-Session (CLAUDE.md 2/13.1).

    Pimpl: der Link-/asio-Header bleibt in der .cpp — er zieht plattform-
    spezifische Netzwerk-Header, die nicht in Projekt-Header gehören.

    Link ist ab Konstruktion aktiv (Peer-Discovery im lokalen Netz) inkl.
    Start/Stop-Sync. Ohne Peers verhält sich die Session wie eine lokale
    Clock — der Session-Beat läuft auf der Wall-Clock weiter.

    Thread-Ownership:
      - ctor/dtor, setTempo(), getTempo(), getNumPeers() → Message Thread
        (Link kapselt die Synchronisation mit seinen Netzwerk-Threads)
      - prepare()             → Message Thread, Audio gestoppt
      - captureClockState()   → Audio Thread, lock-free (Link-Garantie)
*/
class LinkClock final : public IClockSource
{
public:
    explicit LinkClock (double initialBpm = 120.0);
    ~LinkClock() override;

    /** Vor Audio-Start (EngineProcessor::prepareToPlay). */
    void prepare (double sampleRate) noexcept;

    //==========================================================================
    // Message Thread
    void setTempo (double bpm);
    [[nodiscard]] double getTempo() const;
    [[nodiscard]] std::size_t getNumPeers() const;

    //==========================================================================
    // Audio Thread
    [[nodiscard]] ClockState captureClockState (int numSamples) noexcept override;

private:
    static constexpr double quantum = 4.0;  // 4/4 — Phase-Sync auf Takt-Ebene

    struct Impl;
    std::unique_ptr<Impl> impl;

    std::atomic<double> currentSampleRate { 48000.0 };
};

} // namespace conduit
