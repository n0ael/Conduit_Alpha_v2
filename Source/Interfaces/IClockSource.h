#pragma once

namespace conduit
{

//==============================================================================
/** Takt-Schnappschuss für einen Audio-Block — POD, wird einmal pro Block vom
    Clock-Master erzeugt und über den ClockBus an alle IClockSlaves verteilt. */
struct ClockState
{
    double bpm              = 120.0;
    double beatAtBlockStart = 0.0;      // in Beats (Viertelnoten), Session-Zeitachse
    double sampleRate       = 48000.0;
    bool   isPlaying        = false;    // Link Start/Stop-Sync (Session-Beat läuft trotzdem weiter)

    /** Beat-Fortschritt pro Sample — für sample-genaue Phasen in processBlock(). */
    [[nodiscard]] double beatsPerSample() const noexcept
    {
        return sampleRate > 0.0 ? bpm / (60.0 * sampleRate) : 0.0;
    }
};

//==============================================================================
/** Verteiler zwischen Clock-Master und Slaves.

    Thread-Ownership: current wird GENAU EINMAL pro Block vom EngineProcessor
    auf dem Audio Thread geschrieben — VOR graph.processBlock(). Die Slaves
    lesen im selben Callback (der Graph rendert auf demselben Thread), daher
    bewusst kein Atomic. Der Message Thread fasst den Bus nie an. */
struct ClockBus
{
    ClockState current;
};

//==============================================================================
/**
    Mixin-Interface: erzeugt Takt (CLAUDE.md 4.2) → Audio Thread.

    Implementierungen: LinkClock (Ableton-Link-Session); später interne
    Clock-/Transport-Module.
*/
class IClockSource
{
public:
    virtual ~IClockSource() = default;

    /** Audio Thread, einmal pro Block vor dem Graph-Render. numSamples ist
        für spätere Latenz-Kompensation reserviert (Host-Zeit am Block-Ende). */
    [[nodiscard]] virtual ClockState captureClockState (int numSamples) noexcept = 0;
};

} // namespace conduit
