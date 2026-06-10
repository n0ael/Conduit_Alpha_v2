#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace conduit
{

class EngineProcessor;

//==============================================================================
/**
    Haupt-Editor der App — read/listen-only (CLAUDE.md 6).

    Bindet sich an den Root-ValueTree, niemals an Processor-Interna.
    UI-Components für einzelne Module halten später ausschließlich
    ValueTree-Subtree-Referenzen (Zombie-UI-Schutz, CLAUDE.md 5.3).
*/
class EngineEditor final : public juce::AudioProcessorEditor
{
public:
    explicit EngineEditor (EngineProcessor& engineProcessor);
    ~EngineEditor() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::ValueTree rootState;  // ref-counted Handle, read/listen-only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineEditor)
};

} // namespace conduit
