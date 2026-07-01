#include "EngineProcessor.h"

#include "EngineEditor.h"
#include "Util/RtAllocationGuard.h"
#include "Util/ScaleQuantizer.h"

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

    // Link-Audio-Kontext — ILinkAudioClients (Send-Module) bekommen Clock +
    // moduleId bei der Materialisierung (7.2)
    graphManager.setLinkClock (&linkClock);

    // Capture-Kontext — ICaptureTapClients (Tap-Module) bekommen Service +
    // moduleId bei der Materialisierung (Spurname == moduleId)
    graphManager.setCaptureService (&captureService);

    // Kanal-Namen — Auto-Naming der Send-Kanäle (7.2): Quelle am audio_input
    // liefert ihr ChannelNames-Label
    graphManager.setChannelNames (&channelNames);

    // Globale Session-Skala (6.2): Defaults sicherstellen, Properties spiegeln
    ensureSessionScaleDefaults();
    rootState.addListener (this);

    // Resize-Policy der Capture-Settings gegen den Service verdrahten
    // (Aktivitäts-Check, Invalidierung, Reallokation — CaptureSettings-Doku)
    captureSettings.setBufferHost (&captureService);

    // Export-Dateinamen nutzen die ChannelNames-Labels (eine Quelle für
    // CapturePanel, Dateinamen und Port-Beschriftung) — sanitiert; leeres
    // Ergebnis fällt im Service auf "in{N}" zurück
    captureService.hardwareTrackName = [this] (int channel)
    {
        return ChannelNames::sanitizeFileLabel (
            channelNames.getLabel (ChannelNames::Direction::input, channel), {});
    };
}

EngineProcessor::~EngineProcessor()
{
    rootState.removeListener (this);
}

//==============================================================================
void EngineProcessor::ensureSessionScaleDefaults()
{
    if (! rootState.hasProperty (id::scaleRoot))
        rootState.setProperty (id::scaleRoot, 0, nullptr);

    if (! rootState.hasProperty (id::scaleType))
        rootState.setProperty (id::scaleType, toString (ScaleType::chromatic), nullptr);

    refreshScaleAtomics();
}

void EngineProcessor::refreshScaleAtomics()
{
    scaleRootAtomic.store (juce::jlimit (0, 11, (int) rootState.getProperty (id::scaleRoot, 0)),
                           std::memory_order_relaxed);
    scaleTypeAtomic.store (static_cast<int> (scaleTypeFromString (
                               rootState.getProperty (id::scaleType).toString())),
                           std::memory_order_relaxed);
}

void EngineProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == rootState && (property == id::scaleRoot || property == id::scaleType))
        refreshScaleAtomics();
}

//==============================================================================
void EngineProcessor::ensureIONodeStates()
{
    auto nodesTree = rootState.getChildWithName (id::nodes);

    const auto ensure = [&nodesTree] (const char* factoryKey, const char* defaultName,
                                      int numInputs, int numOutputs, int x, int y)
    {
        // Vorhanden? factoryId-Match; Alt-Bestände tragen den Schlüssel in moduleId
        if (nodesTree.getChildWithProperty (id::factoryId, juce::String (factoryKey)).isValid()
            || nodesTree.getChildWithProperty (id::moduleId, juce::String (factoryKey)).isValid())
            return;

        juce::ValueTree node (id::node);
        node.setProperty (id::nodeId,            juce::Uuid().toString(),          nullptr);
        node.setProperty (id::type,              toString (ModuleType::io),        nullptr);
        node.setProperty (id::factoryId,         factoryKey,                       nullptr);
        node.setProperty (id::moduleId,          defaultName,                      nullptr);
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
    ensure (audioInputModuleId,  "audio_in",  0, 2, 40,  260);
    ensure (audioOutputModuleId, "audio_out", 2, 0, 700, 260);
}

void EngineProcessor::syncHardwareIOChannels (int deviceInputs, int deviceOutputs)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodesTree = rootState.getChildWithName (id::nodes);

    const auto apply = [&nodesTree] (const char* factoryKey,
                                     const juce::Identifier& channelProp, int count)
    {
        auto node = nodesTree.getChildWithProperty (id::factoryId, juce::String (factoryKey));
        if (! node.isValid())
            node = nodesTree.getChildWithProperty (id::moduleId, juce::String (factoryKey));

        // Idempotent: nur bei echter Abweichung schreiben, sonst löst jeder
        // Gerätewechsel unnötige UI-Rebuilds aus. Geräte-getrieben → kein Undo.
        if (node.isValid() && (int) node.getProperty (channelProp, -1) != count)
            node.setProperty (channelProp, count, nullptr);
    };

    // Input-Prozessor LIEFERT Kanäle → Ausgangs-Ports am audio_in-Node;
    // Output-Prozessor NIMMT Kanäle → Eingangs-Ports am audio_out-Node
    apply (audioInputModuleId,  id::numOutputChannels, juce::jmax (0, deviceInputs));
    apply (audioOutputModuleId, id::numInputChannels,  juce::jmax (0, deviceOutputs));
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
    captureService.prepare (sampleRate, samplesPerBlock, getTotalNumInputChannels());
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

    // Capture-Input-Tap als ERSTE Operation: hier liegt noch der rohe
    // Hardware-Input im Buffer — der Graph überschreibt ihn gleich mit
    // Modul-Outputs und der GraphFader blendet bei Swaps; beides gehört
    // nicht in Metering und (spätere) Aufzeichnung.
    {
        // RT-Audit (Dev-Builds): der Tap muss allocation-free sein — jede
        // new/delete in diesem Abschnitt zählt als Violation und hält unter
        // dem Debugger sofort an (Util/RtAllocationGuard)
        const rt::ScopedRealtimeSection rtAudit;
        captureService.processInputTap (buffer, getTotalNumInputChannels());
    }

    // Pfad 1 des OSC-Dual-State (6.1): Queue VOR dem Graph vollständig
    // dränieren — lock-free, allocation-free, < 1ms vom Empfang bis hier
    ParameterUpdate update;
    while (oscToAudioQueue.pop (update))
        if (update.target != nullptr)
            update.target->store (update.value, std::memory_order_relaxed);

    // Session-Kontext einmal pro Block einfangen — die IClockSlaves im
    // Graph lesen den Bus im selben Callback (4.2)
    clockBus.current = linkClock.captureClockState (buffer.getNumSamples());
    clockBus.current.scaleRootNote  = scaleRootAtomic.load (std::memory_order_relaxed);
    clockBus.current.scaleTypeIndex = scaleTypeAtomic.load (std::memory_order_relaxed);

    graph.processBlock (buffer, midiMessages);
    graphFader.process (buffer);  // Master-Fade hinter dem Graph (5.2)
}

bool EngineProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Beide Busse tragen genau eine (Haupt-)Bus-Gruppe; wir nehmen jede
    // Kanalzahl an. Der AudioProcessorPlayer probiert die echte Device-I/O-
    // Zahl vor dem Stereo-Default (findMostSuitableLayout), sodass ein
    // Multichannel-Interface bis in graph.setPlayConfigDetails() durchreicht.
    // Eingänge dürfen 0 sein (Ausgabe-only-Interface, 9.1); Ausgänge sind
    // Conduits Primärzweck, daher mindestens einer.
    return layouts.getMainOutputChannels() >= 1;
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
            ensureIONodeStates();          // Presets ohne I/O-Nodes reparieren
            ensureSessionScaleDefaults();  // ... und ohne Skalen-Properties
        }
    }
}

//==============================================================================
juce::Result EngineProcessor::savePreset (const juce::File& file)
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Serialisierungs-Guard (6.1): kein OSC-Wert geht beim Speichern verloren
    oscController.flushPendingUpdates();

    const auto snapshot = rootState.createCopy();  // CLAUDE.md 5.4

    if (const auto xml = snapshot.createXml())
    {
        if (xml->writeTo (file))
            return juce::Result::ok();

        return juce::Result::fail ("Preset-Datei nicht schreibbar: " + file.getFullPathName());
    }

    return juce::Result::fail ("Preset-Serialisierung fehlgeschlagen");
}

juce::Result EngineProcessor::loadPreset (const juce::File& file)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr)
        return juce::Result::fail ("Kein gültiges Preset-XML: " + file.getFullPathName());

    const auto loaded = juce::ValueTree::fromXml (*xml);

    if (! loaded.hasType (id::root))
        return juce::Result::fail ("Kein Conduit-Preset: " + file.getFullPathName());

    // Undo-fähig in EINER Transaktion — ein Undo stellt den kompletten
    // vorherigen Patch wieder her (Batch-Coalescing 5.5 macht daraus
    // einen einzigen Graph-Swap)
    undoManager.beginNewTransaction ("Preset laden");
    rootState.copyPropertiesAndChildrenFrom (loaded, &undoManager);
    ensureIONodeStates();          // Presets ohne I/O-Nodes reparieren
    ensureSessionScaleDefaults();  // ... und ohne Skalen-Properties

    return juce::Result::ok();
}

//==============================================================================
juce::ValueTree EngineProcessor::getRootState() noexcept       { return rootState; }
juce::UndoManager& EngineProcessor::getUndoManager() noexcept  { return undoManager; }
GraphManager& EngineProcessor::getGraphManager() noexcept      { return graphManager; }
NodeUiRegistry& EngineProcessor::getNodeUiRegistry() noexcept  { return nodeUiRegistry; }
OscController& EngineProcessor::getOscController() noexcept    { return oscController; }
LinkClock& EngineProcessor::getLinkClock() noexcept            { return linkClock; }
const CaptureService& EngineProcessor::getCaptureService() const noexcept { return captureService; }
CaptureService& EngineProcessor::getCaptureService() noexcept   { return captureService; }
CaptureSettings& EngineProcessor::getCaptureSettings() noexcept { return captureSettings; }
ChannelNames& EngineProcessor::getChannelNames() noexcept       { return channelNames; }

} // namespace conduit
