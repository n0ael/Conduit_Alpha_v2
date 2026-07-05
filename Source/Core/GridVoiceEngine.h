#pragma once

#include <cstdint>

#include <juce_core/juce_core.h>

#include "Interfaces/IVoiceSink.h"
#include "VoiceAllocator.h"

namespace conduit::grid
{

//==============================================================================
/**
    Engine-Level-Kern des Grid-Voice-Modells (Muster LooperEngine, CLAUDE.md
    10.0: EngineProcessor-frei, module-ready). Verbindet Touch-Finger-Events
    über den VoiceAllocator mit einem austauschbaren IVoiceSink.

    Thread-Ownership: Message Thread (ITouchMacro-Kette, CLAUDE.md 4.2) —
    keine Thread-Grenze, daher keine Atomics nötig.
*/
class GridVoiceEngine
{
public:
    GridVoiceEngine (IVoiceSink& sinkToUse,
                     int maxVoices = VoiceAllocator::kMaxVoices) noexcept;

    void noteOn       (uint32_t fingerId, int note, int velocity) noexcept;
    void noteOff      (uint32_t fingerId, int releaseVelocity) noexcept;
    void setPitchBend (uint32_t fingerId, float semitones) noexcept;
    void setPressure  (uint32_t fingerId, float value01) noexcept;
    void setSlide     (uint32_t fingerId, float value01) noexcept;
    void allNotesOff  () noexcept;

private:
    IVoiceSink&    sink;
    VoiceAllocator allocator;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridVoiceEngine)
};

} // namespace conduit::grid
