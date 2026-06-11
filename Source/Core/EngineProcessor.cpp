#include "EngineProcessor.h"

#include "EngineEditor.h"

namespace conduit
{

EngineProcessor::EngineProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      rootState (id::root)
{
    // Schema 6.2 — die drei Top-Level-Container des Root-Trees
    rootState.appendChild (juce::ValueTree (id::nodes),               nullptr);
    rootState.appendChild (juce::ValueTree (id::connections),         nullptr);
    rootState.appendChild (juce::ValueTree (id::calibrationProfiles), nullptr);

    registerDefaultModules (moduleFactory);

    // Graph-I/O-Nodes (Konstruktor läuft auf dem Message Thread).
    // Bewusst keine Verbindung Input→Output — kein Mic-Passthrough.
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    audioInputNode  = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioInputNode));
    audioOutputNode = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioOutputNode));
}

//==============================================================================
void EngineProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    graph.setPlayConfigDetails (getTotalNumInputChannels(),
                                getTotalNumOutputChannels(),
                                sampleRate, samplesPerBlock);
    graph.prepareToPlay (sampleRate, samplesPerBlock);
    graphFader.prepare (sampleRate);
}

void EngineProcessor::releaseResources()
{
    graphFader.reset();  // unprepared Fader → GraphManager swappt ohne Fade
    graph.releaseResources();

    // Audio steht — der Message Thread darf als Consumer einspringen und
    // verwirft liegengebliebene OSC-Updates. Schließt das Lebensdauer-Fenster
    // der target-Pointer, falls Module bei gestopptem Audio zerstört werden.
    ParameterUpdate discarded;
    while (oscToAudioQueue.pop (discarded)) {}
}

void EngineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Pfad 1 des OSC-Dual-State (6.1): Queue VOR dem Graph vollständig
    // dränieren — lock-free, allocation-free, < 1ms vom Empfang bis hier
    ParameterUpdate update;
    while (oscToAudioQueue.pop (update))
        if (update.target != nullptr)
            update.target->store (update.value, std::memory_order_relaxed);

    graph.processBlock (buffer, midiMessages);
    graphFader.process (buffer);  // Master-Fade hinter dem Graph (5.2)
}

//==============================================================================
juce::AudioProcessorEditor* EngineProcessor::createEditor() { return new EngineEditor (*this); }
bool EngineProcessor::hasEditor() const                     { return true; }

const juce::String EngineProcessor::getName() const         { return "Conduit Engine"; }
bool EngineProcessor::acceptsMidi() const                   { return true; }
bool EngineProcessor::producesMidi() const                  { return false; }
double EngineProcessor::getTailLengthSeconds() const        { return 0.0; }

int EngineProcessor::getNumPrograms()                       { return 1; }
int EngineProcessor::getCurrentProgram()                    { return 0; }
void EngineProcessor::setCurrentProgram (int)               {}
const juce::String EngineProcessor::getProgramName (int)    { return {}; }
void EngineProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void EngineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Serialisierungs-Guard (6.1): ausstehende OSC-Werte synchron in den
    // Tree flushen, damit beim Speichern nichts verloren geht.
    oscController.flushPendingUpdates();

    // Snapshot des Root-Trees (CLAUDE.md 5.4)
    const auto snapshot = rootState.createCopy();

    if (const auto xml = snapshot.createXml())
        copyXmlToBinary (*xml, destData);
}

void EngineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        const auto loaded = juce::ValueTree::fromXml (*xml);

        if (loaded.hasType (id::root))
            rootState.copyPropertiesAndChildrenFrom (loaded, nullptr);
    }
}

//==============================================================================
juce::ValueTree EngineProcessor::getRootState() noexcept       { return rootState; }
juce::UndoManager& EngineProcessor::getUndoManager() noexcept  { return undoManager; }
GraphManager& EngineProcessor::getGraphManager() noexcept      { return graphManager; }
NodeUiRegistry& EngineProcessor::getNodeUiRegistry() noexcept  { return nodeUiRegistry; }
OscController& EngineProcessor::getOscController() noexcept    { return oscController; }

} // namespace conduit
