#pragma once

#include <juce_core/juce_core.h>

namespace touchlab
{

//==============================================================================
/** Herkunft eines Samples — die beiden verglichenen Datenpfade. */
enum class SourceTag
{
    Native,      // JUCE MouseEvent (Windows' vorverarbeitete Koordinate)
    RawPointer   // WM_POINTER ptPixelLocationRaw (unprädizierte Digitizer-Koordinate)
};

enum class Phase { Down, Move, Up };

//==============================================================================
/**
    Gemeinsamer POD, den BEIDE Quellen in denselben Sink schieben — damit ist
    die Pipeline quellen-agnostisch (Trennung Datenquelle/Verarbeitung).
    Koordinaten sind lokal zur Trace-Fläche (Referenz-Component).
*/
struct TouchSample
{
    float     x = 0.0f;
    float     y = 0.0f;
    int       contactId = 0;
    Phase     phase = Phase::Move;
    double    tSeconds = 0.0;   // juce::Time::getMillisecondCounterHiRes() * 0.001
    SourceTag tag = SourceTag::Native;
};

//==============================================================================
/** Die Naht: beide Quellen kennen nur diesen Sink, nichts weiter. */
struct TouchSink
{
    virtual ~TouchSink() = default;
    virtual void pushSample (const TouchSample& sample) = 0;
};

} // namespace touchlab
