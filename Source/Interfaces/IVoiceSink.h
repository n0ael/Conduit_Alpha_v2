#pragma once

namespace conduit::grid
{

//==============================================================================
/**
    Abstrakter Ausgang des Grid-Voice-Modells. Nimmt bereits zugeteilte,
    Voice-Slot-indizierte Ausdrucks-Events entgegen und übersetzt sie in ein
    konkretes Ziel (MPE-MIDI, OSC, CV — je Implementierung).

    Thread-Ownership: ALLE Methoden werden auf dem Message Thread aufgerufen
    (ITouchMacro-Kette, CLAUDE.md 4.2). Implementierungen, die Thread-Grenzen
    überschreiten, kapseln das intern (SpscQueue/std::atomic, CLAUDE.md 3.1).

    voiceIndex ist 0-basiert und < maxVoices der zugehörigen Engine.
    value-Parameter (Pressure/Slide) sind auf [0, 1] normalisiert. Die
    Achsen-Namen folgen der MPE-Konvention; andere Sinks lesen sie als
    generische Ausdrucksachse A (pressure) / B (slide).
*/
class IVoiceSink
{
public:
    virtual ~IVoiceSink() = default;

    virtual void voiceStart     (int voiceIndex, int note, int velocity) = 0;
    virtual void voiceStop      (int voiceIndex, int releaseVelocity)    = 0;
    virtual void voicePitchBend (int voiceIndex, float semitones)        = 0;
    virtual void voicePressure  (int voiceIndex, float value)            = 0;
    virtual void voiceSlide     (int voiceIndex, float value)            = 0;
    virtual void allNotesOff    ()                                       = 0;
};

} // namespace conduit::grid
