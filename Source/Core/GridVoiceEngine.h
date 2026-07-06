#pragma once

#include <array>
#include <cstdint>
#include <vector>

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

    /** Bipolarer globaler Slide-Offset [-1, +1] — analog setPressureOffset. */
    void setSlideOffset (float bipolarOffset) noexcept;

    /** Bipolarer globaler PitchBend-Offset in Halbtönen, intern auf ±12 HT
        geklemmt — addiert auf den per-Note-Bend jeder gehaltenen Stimme;
        Ausgang bleibt auf die Encoder-Bendrange (±kPitchBendRangeSemitones)
        geklemmt. Message Thread. */
    void setPitchBendOffset (float bipolarOffsetSemitones) noexcept;

    //==========================================================================
    // Lesepfade fürs spätere MPE-Shaping-Panel (S2) — reiner Lese-/
    // Referenzzugriff, keine Verhaltensänderung am Spielpfad.

    enum class Axis { Pressure, Slide, PitchBend };

    struct VoiceReadout
    {
        int   voiceIndex = -1;
        int   note       = -1;   // MIDI-Note der Stimme
        float rawValue   = 0.0f; // roher Achsen-Eingang (vor Kurve/Offset)
    };

    /** Nur-Lese-Referenz auf die ResponseCurve einer Achse (Panel-Editor). */
    ResponseCurve&       responseCurve (Axis axis) noexcept;
    const ResponseCurve& responseCurve (Axis axis) const noexcept;

    /** Füllt outVoices mit je einem Eintrag pro AKTIVER Stimme für die
        gewählte Achse (rawValue der Achse + Note). outVoices wird geleert
        und neu befüllt; keine Allokation, wenn Kapazität ≥ kMaxVoices.
        Message Thread. */
    void readActiveVoices (Axis axis, std::vector<VoiceReadout>& outVoices) const;

private:
    // Encoder-Bendrange (MpeEncoder::Config::pitchBendRangeSemitones-Default,
    // CLAUDE.md 14 ADR) — die Achse kennt die konkrete Encoder-Config nicht
    // (IVoiceSink-Abstraktion), spiegelt aber deren Reichweite für den
    // Ausgangs-Clamp.
    static constexpr float kPitchBendRangeSemitones = 48.0f;

    ExpressionAxis& axisFor (Axis axis) noexcept;
    const ExpressionAxis& axisFor (Axis axis) const noexcept;

    IVoiceSink&    sink;
    VoiceAllocator allocator;

    ExpressionAxis pressureAxis;
    ExpressionAxis slideAxis;
    ExpressionAxis pitchBendAxis;

    // Note pro aktivem Slot (-1 = inaktiv) -- für die Panel-Visualisierung
    // (Noten-Kreise); ohne Einfluss auf den Sink-Pfad.
    std::array<int, VoiceAllocator::kMaxVoices> slotNote;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridVoiceEngine)
};

} // namespace conduit::grid
