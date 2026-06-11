#pragma once

#include <cstdint>

namespace conduit
{

//==============================================================================
/**
    Mixin-Interface: Zufalls-Parameter (CLAUDE.md 4.2) → Audio Thread,
    Seed-Updates → Message Thread.

    Implementierende Module besitzen ihre EIGENE RNG-Instanz (juce::Random)
    und benutzen sie ausschließlich im Audio Thread — kein globaler/geteilter
    Zufall. Der GraphManager injiziert beim Materialisieren (vor der
    Graph-Aufnahme, Muster IClockSlave) einen deterministischen Seed aus der
    nodeUuid — Patterns sind damit pro Node reproduzierbar; Tests setzen den
    Seed direkt.
*/
class IStochastic
{
public:
    virtual ~IStochastic() = default;

    /** Message Thread, vor der Graph-Aufnahme (oder bei gestopptem Audio). */
    virtual void setRandomSeed (std::uint64_t seed) noexcept = 0;
};

} // namespace conduit
