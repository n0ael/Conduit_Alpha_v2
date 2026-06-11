#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "GraphFader.h"
#include "GraphManager.h"
#include "Modules/ConduitModule.h"

namespace conduit
{

//==============================================================================
/**
    Zentraler AudioProcessor der App.

    Besitzt den Root-ValueTree (Single Source of Truth für Zustand und
    Serialisierung, CLAUDE.md 6) sowie den juce::AudioProcessorGraph als
    DSP-Engine (CLAUDE.md 5.1). Der Editor ist read/listen-only.

    Graph-Mutationen (addNode / removeConnection) laufen ausschließlich auf
    dem Message Thread — später koordiniert durch den GraphManager.
*/
class EngineProcessor final : public juce::AudioProcessor
{
public:
    EngineProcessor();
    ~EngineProcessor() override = default;

    //==========================================================================
    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Datenmodell — Mutationen NUR auf dem Message Thread (CLAUDE.md 6)
    [[nodiscard]] juce::ValueTree getRootState() noexcept;
    [[nodiscard]] juce::UndoManager& getUndoManager() noexcept;

private:
    juce::ValueTree rootState;
    juce::UndoManager undoManager;

    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::Node::Ptr audioInputNode;
    juce::AudioProcessorGraph::Node::Ptr audioOutputNode;

    // Master-Fade für glitch-freie Graph-Swaps (CLAUDE.md 5.2)
    GraphFader graphFader;

    // Nach rootState, graph und graphFader deklariert — Initialisierungsreihenfolge!
    GraphManager graphManager { rootState, graph, graphFader };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineProcessor)
};

} // namespace conduit
