#include "NativeTouchSource.h"

namespace touchlab
{

NativeTouchSource::NativeTouchSource (TouchSink& sinkToUse)
    : sink (sinkToUse)
{
    setWantsKeyboardFocus (false);
    // Overlay fängt Klicks selbst; die darunterliegende TraceView zeichnet nur.
}

void NativeTouchSource::emit (const juce::MouseEvent& e, Phase phase)
{
    TouchSample s;
    s.x = e.position.x;
    s.y = e.position.y;
    s.contactId = e.source.getIndex();
    s.phase = phase;
    s.tSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    s.tag = SourceTag::Native;
    sink.pushSample (s);
}

void NativeTouchSource::mouseDown (const juce::MouseEvent& e) { emit (e, Phase::Down); }
void NativeTouchSource::mouseDrag (const juce::MouseEvent& e) { emit (e, Phase::Move); }
void NativeTouchSource::mouseUp   (const juce::MouseEvent& e) { emit (e, Phase::Up); }

} // namespace touchlab
