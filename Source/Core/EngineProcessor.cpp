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
}

void EngineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
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
    // Snapshot des Root-Trees (CLAUDE.md 5.4). Der isDirty-Guard (6.1)
    // folgt zusammen mit dem OscController.
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
juce::ValueTree EngineProcessor::getRootState() noexcept     { return rootState; }
juce::UndoManager& EngineProcessor::getUndoManager() noexcept { return undoManager; }

} // namespace conduit
