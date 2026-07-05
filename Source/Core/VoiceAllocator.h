#pragma once

#include <array>
#include <cstdint>

#include <juce_core/juce_core.h>

namespace conduit::grid
{

//==============================================================================
/**
    Teilt Touch-Finger (fingerId, > 0) festen Voice-Slots zu. Reine Logik,
    allocation-free, thread-agnostisch — dieselbe Klasse bedient später den
    Message-Thread-MIDI-Sink UND (CV-Sink-Meilenstein) den Audio-Thread.

    Stealing: bei Erschöpfung wird die ÄLTESTE aktive Stimme verdrängt.
    fingerId == 0 ist reserviert (frei-Sentinel) und wird nie zugeteilt.
*/
class VoiceAllocator
{
public:
    static constexpr int kMaxVoices = 15; // MPE Lower Zone (Member 2..16)

    explicit VoiceAllocator (int maxVoices = kMaxVoices) noexcept;

    /** fingerId anmelden → voiceIndex (0..maxVoices-1). Bereits aktiver
        fingerId → derselbe Slot (idempotent, stolenFingerOut = 0).
        Alle Slots belegt → ältesten stehlen; stolenFingerOut = verdrängter
        fingerId (0 wenn keiner), damit der Aufrufer dessen voiceStop sendet. */
    int  allocate        (uint32_t fingerId, uint32_t& stolenFingerOut) noexcept;
    int  release         (uint32_t fingerId) noexcept;   // → voiceIndex, sonst -1
    int  voiceForFinger  (uint32_t fingerId) const noexcept; // → voiceIndex, sonst -1
    void reset           () noexcept;
    int  activeVoices    () const noexcept;
    int  maxVoices       () const noexcept { return numVoices; }

private:
    int numVoices;
    std::array<uint32_t, kMaxVoices> slotFinger { };
    std::array<uint64_t, kMaxVoices> slotAge    { };
    uint64_t ageCounter { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceAllocator)
};

} // namespace conduit::grid
