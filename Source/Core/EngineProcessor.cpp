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

    // Als externe Endpunkte verfügbar machen: Tree-Nodes mit diesen
    // moduleIds mappen auf die I/O-Prozessoren statt auf Factory-Module
    graphManager.registerExternalEndpoint (audioInputModuleId,  audioInputNode->nodeID);
    graphManager.registerExternalEndpoint (audioOutputModuleId, audioOutputNode->nodeID);
    ensureIONodeStates();

    // Takt-Verteiler — IClockSlaves bekommen den Bus bei der Materialisierung
    graphManager.setClockBus (&clockBus);
}

//==============================================================================
void EngineProcessor::ensureIONodeStates()
{
    auto nodesTree = rootState.getChildWithName (id::nodes);

    const auto ensure = [&nodesTree] (const char* moduleId, int numInputs, int numOutputs,
                                      int x, int y)
    {
        if (nodesTree.getChildWithProperty (id::moduleId, juce::String (moduleId)).isValid())
            return;

        juce::ValueTree node (id::node);
        node.setProperty (id::nodeId,            juce::Uuid().toString(),          nullptr);
        node.setProperty (id::type,              toString (ModuleType::io),        nullptr);
        node.setProperty (id::moduleId,          moduleId,                         nullptr);
        node.setProperty (id::stateVersion,      1,                                nullptr);
        node.setProperty (id::nodeState,         toString (NodeState::active),     nullptr);
        node.setProperty (id::nodeError,         juce::String(),                   nullptr);
        node.setProperty (id::positionX,         x,                                nullptr);
        node.setProperty (id::positionY,         y,                                nullptr);
        node.setProperty (id::numInputChannels,  numInputs,                        nullptr);
        node.setProperty (id::numOutputChannels, numOutputs,                       nullptr);
        node.appendChild (juce::ValueTree (id::parameters), nullptr);

        nodesTree.appendChild (node, nullptr);  // Grundausstattung — kein Undo
    };

    // Aus Graph-Sicht: der Input-Prozessor LIEFERT Kanäle (Outputs),
    // der Output-Prozessor NIMMT Kanäle entgegen (Inputs)
    ensure (audioInputModuleId,  0, 2, 40,  260);
    ensure (audioOutputModuleId, 2, 0, 700, 260);
}

//==============================================================================
void EngineProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    graph.setPlayConfigDetails (getTotalNumInputChannels(),
                                getTotalNumOutputChannels(),
                                sampleRate, samplesPerBlock);
    graph.prepareToPlay (sampleRate, samplesPerBlock);
    graphFader.prepare (sampleRate);
    linkClock.prepare (sampleRate);
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

    // Takt einmal pro Block einfangen — die IClockSlaves im Graph lesen
    // den Bus im selben Callback (4.2)
    clockBus.current = linkClock.captureClockState (buffer.getNumSamples());

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
        {
            rootState.copyPropertiesAndChildrenFrom (loaded, nullptr);
            ensureIONodeStates();  // Presets ohne I/O-Nodes reparieren
        }
    }
}

//==============================================================================
juce::ValueTree EngineProcessor::getRootState() noexcept       { return rootState; }
juce::UndoManager& EngineProcessor::getUndoManager() noexcept  { return undoManager; }
GraphManager& EngineProcessor::getGraphManager() noexcept      { return graphManager; }
NodeUiRegistry& EngineProcessor::getNodeUiRegistry() noexcept  { return nodeUiRegistry; }
OscController& EngineProcessor::getOscController() noexcept    { return oscController; }
LinkClock& EngineProcessor::getLinkClock() noexcept            { return linkClock; }

} // namespace conduit
