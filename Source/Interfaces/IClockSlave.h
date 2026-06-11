#pragma once

#include "IClockSource.h"

namespace conduit
{

//==============================================================================
/**
    Mixin-Interface: konsumiert Takt (CLAUDE.md 4.2) → Audio Thread.

    Der GraphManager injiziert den ClockBus bei der Materialisierung
    (5.2 Schritt 1, Message Thread) — BEVOR das Modul in den Graph kommt,
    es gibt also kein Race mit processBlock(). Der Bus überdauert jedes
    Modul (Owner: EngineProcessor).

    Ohne Bus (nullptr, z.B. in Tests) laufen Slaves frei.
*/
class IClockSlave
{
public:
    virtual ~IClockSlave() = default;

    /** Message Thread, vor der Graph-Aufnahme. */
    virtual void setClockBus (const ClockBus* bus) noexcept = 0;
};

} // namespace conduit
