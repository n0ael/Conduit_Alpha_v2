#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "GraphFader.h"
#include "GraphManager.h"
#include "LinkClock.h"
#include "NodeUiRegistry.h"
#include "OscController.h"
#include "Interfaces/IClockSource.h"
#include "Modules/ConduitModule.h"
#include "Modules/ModuleFactory.h"
#include "Util/SpscQueue.h"

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

    /** Patch-Aktionen (Delete-Requests) und UI-Bindungs-Registry. */
    [[nodiscard]] GraphManager& getGraphManager() noexcept;
    [[nodiscard]] NodeUiRegistry& getNodeUiRegistry() noexcept;
    [[nodiscard]] OscController& getOscController() noexcept;
    [[nodiscard]] LinkClock& getLinkClock() noexcept;

private:
    /** Legt die reservierten I/O-Tree-Nodes (audio_input/audio_output) an,
        falls sie fehlen — frischer Patch oder Preset ohne I/O. Idempotent. */
    void ensureIONodeStates();

    juce::ValueTree rootState;
    juce::UndoManager undoManager;

    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::Node::Ptr audioInputNode;
    juce::AudioProcessorGraph::Node::Ptr audioOutputNode;

    // Master-Fade für glitch-freie Graph-Swaps (CLAUDE.md 5.2)
    GraphFader graphFader;

    // Clock-Master (Ableton-Link-Session) + Takt-Verteiler (4.2):
    // clockBus wird einmal pro Block auf dem Audio Thread gefüllt,
    // IClockSlaves lesen im selben Callback
    LinkClock linkClock;
    ClockBus clockBus;

    // Default-Module werden im Konstruktor registriert
    ModuleFactory moduleFactory;

    // Zombie-UI-Schutz für das zweiphasige Delete (CLAUDE.md 5.3)
    NodeUiRegistry nodeUiRegistry;

    // Nach allen Abhängigkeiten deklariert — Initialisierungsreihenfolge!
    GraphManager graphManager { rootState, graph, graphFader,
                                moduleFactory, undoManager, nodeUiRegistry };

    // OSC-Dual-State (CLAUDE.md 6.1): Netzwerk → Audio (lock-free) und
    // Netzwerk → ValueTree (async). Nach dem GraphManager deklariert —
    // der OscController löst Endpoints über ihn auf und wird zuerst zerstört.
    SpscQueue<ParameterUpdate> oscToAudioQueue { 1024 };
    OscController oscController { rootState, graphManager, oscToAudioQueue };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineProcessor)
};

} // namespace conduit
