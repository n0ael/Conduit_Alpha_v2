#pragma once

#include <atomic>
#include <cstdint>

namespace conduit
{

//==============================================================================
/**
    Globale Sample-Clock des Capture-Systems.

    Zählt monoton alle Samples, die der Input-Tap seit dem letzten reset()
    gesehen hat. advance() ruft ausschließlich der Audio Thread am ENDE von
    CaptureService::processInputTap() auf; now() darf jeder Thread lesen.

    Memory-Ordering analog zu den Skalen-Atomics im EngineProcessor — ein
    einzelnes Wort, kein Mutex. Hier release/acquire statt relaxed: Leser,
    die später Ring-Inhalte bis zu einer gelesenen Position konsumieren
    (PreRoll-Baustein), sehen damit garantiert alle Samples vor dieser
    Position.

    reset() gehört in prepareToPlay: ein Samplerate-Wechsel invalidiert
    alle früheren Positionen — Konsumenten müssen neu aufsetzen.
*/
class SampleClock
{
public:
    SampleClock() = default;

    /** [Audio Thread] Am Ende des Input-Taps aufrufen. */
    void advance (int numSamples) noexcept
    {
        if (numSamples > 0)
            position.fetch_add (static_cast<std::uint64_t> (numSamples),
                                std::memory_order_release);
    }

    /** [beliebiger Thread] Position in Samples seit dem letzten reset(). */
    [[nodiscard]] std::uint64_t now() const noexcept
    {
        return position.load (std::memory_order_acquire);
    }

    /** [Message Thread, aus prepareToPlay — Audio steht] */
    void reset() noexcept
    {
        position.store (0, std::memory_order_release);
    }

private:
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free,
                   "SampleClock muss lock-free sein (Audio Thread)");

    // 64 bit überlaufen praktisch nie: selbst bei 192 kHz erst nach
    // ~3 Millionen Jahren Dauerbetrieb — kein Wrap-Handling nötig.
    std::atomic<std::uint64_t> position { 0 };
};

} // namespace conduit
