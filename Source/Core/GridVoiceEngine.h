#pragma once

#include <cstdint>

#include <juce_core/juce_core.h>

#include "ExpressionAxis.h"
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

    /** Globaler Master-Kanal-Controller (nicht Voice-indiziert) — delegiert
        1:1 an den Sink. */
    void setGlobalVolume (float value01) noexcept;

    /** Bipolarer globaler Pressure-Offset [-1, +1]. Wird intern auf jeden
        per-Voice-Pressure addiert (clamp [0,1]) und sofort auf alle
        gehaltenen Stimmen angewandt. Message Thread. */
    void setPressureOffset (float bipolarOffset) noexcept;

private:
    IVoiceSink&    sink;
    VoiceAllocator allocator;

    ExpressionAxis pressureAxis;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridVoiceEngine)
};

} // namespace conduit::grid
