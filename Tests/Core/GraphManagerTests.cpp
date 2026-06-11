#include <catch2/catch_test_macros.hpp>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Modules/AttenuatorModule.h"
#include "Modules/ModuleFactory.h"
#include "Modules/UtilityModule.h"

namespace
{

juce::ValueTree makeRootTree()
{
    juce::ValueTree root (conduit::id::root);
    root.appendChild (juce::ValueTree (conduit::id::nodes),               nullptr);
    root.appendChild (juce::ValueTree (conduit::id::connections),         nullptr);
    root.appendChild (juce::ValueTree (conduit::id::calibrationProfiles), nullptr);
    return root;
}

juce::ValueTree makeModuleNode (const juce::String& moduleId)
{
    juce::ValueTree node (conduit::id::node);
    node.setProperty (conduit::id::nodeId, juce::Uuid().toString(), nullptr);
    node.setProperty (conduit::id::moduleId, moduleId, nullptr);
    return node;
}

juce::ValueTree makeConnection (const juce::ValueTree& source, int sourceChannel,
                                const juce::ValueTree& dest, int destChannel)
{
    juce::ValueTree connection (conduit::id::connection);
    connection.setProperty (conduit::id::sourceNodeId,  source.getProperty (conduit::id::nodeId), nullptr);
    connection.setProperty (conduit::id::sourceChannel, sourceChannel, nullptr);
    connection.setProperty (conduit::id::destNodeId,    dest.getProperty (conduit::id::nodeId), nullptr);
    connection.setProperty (conduit::id::destChannel,   destChannel, nullptr);
    return connection;
}

void pumpUntilSilent (conduit::GraphFader& fader)
{
    juce::AudioBuffer<float> buffer (2, 32);

    for (int i = 0; i < 100 && ! fader.isFadeOutComplete(); ++i)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (channel),
                                               1.0f, buffer.getNumSamples());
        fader.process (buffer);
    }
}

// Stub für den nodeError-Pfad: prepareForGraph schlägt kontrolliert fehl
class FailingModule final : public conduit::UtilityModule
{
public:
    FailingModule()
        : conduit::UtilityModule (BusesProperties()
              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    juce::String getModuleId() const override          { return "failing_test_module"; }
    juce::String getModuleDisplayName() const override { return "Failing Test Module"; }
    int getStateVersion() const override               { return 1; }

    juce::Result prepareForGraph (double, int) override
    {
        return juce::Result::fail ("Allokation fehlgeschlagen (Test)");
    }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
};

void registerTestModules (conduit::ModuleFactory& factory)
{
    conduit::registerDefaultModules (factory);
    factory.registerModule ("failing_test_module",
                            [] { return std::make_unique<FailingModule>(); });
}

} // namespace

//==============================================================================
TEST_CASE ("Batch-Coalescing: viele Topologie-Änderungen in einem Frame ergeben einen Rebuild",
           "[GraphManager]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;  // unprepared → Swap ohne Fade
    conduit::ModuleFactory factory;
    conduit::registerDefaultModules (factory);
    conduit::GraphManager manager (root, graph, fader, factory);

    auto nodes       = root.getChildWithName (conduit::id::nodes);
    auto connections = root.getChildWithName (conduit::id::connections);

    // Bulk-Szenario aus CLAUDE.md 5.5: 5 Module + 20 Kabel in einem Frame
    for (int i = 0; i < 5; ++i)
        nodes.appendChild (makeModuleNode (conduit::AttenuatorModule::staticModuleId), nullptr);

    for (int i = 0; i < 20; ++i)
        connections.appendChild (juce::ValueTree (conduit::id::connection), nullptr);

    REQUIRE (manager.isTopologyDirty());
    REQUIRE (manager.getRebuildCount() == 0);  // noch kein Loop-Durchlauf

    manager.flushPendingTopologyUpdate();      // = nächster Message-Loop-Durchlauf

    CHECK (manager.getRebuildCount() == 1);    // 25 Änderungen → genau 1 Rebuild
    CHECK_FALSE (manager.isTopologyDirty());
    CHECK (graph.getNumNodes() == 5);          // alle 5 Module materialisiert

    // Folge-Durchlauf ohne neue Änderungen darf nichts tun
    manager.flushPendingTopologyUpdate();
    CHECK (manager.getRebuildCount() == 1);
}

//==============================================================================
TEST_CASE ("Parameter-Änderungen lösen keinen Graph-Rebuild aus", "[GraphManager]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    conduit::registerDefaultModules (factory);
    conduit::GraphManager manager (root, graph, fader, factory);

    auto nodes = root.getChildWithName (conduit::id::nodes);
    auto node = makeModuleNode (conduit::AttenuatorModule::staticModuleId);
    nodes.appendChild (node, nullptr);
    manager.flushPendingTopologyUpdate();
    REQUIRE (manager.getRebuildCount() == 1);

    // Parameters-Subtree unterhalb eines Nodes ist KEINE Topologie-Änderung
    juce::ValueTree parameters (conduit::id::parameters);
    juce::ValueTree parameter (conduit::id::parameter);
    parameters.appendChild (parameter, nullptr);
    node.appendChild (parameters, nullptr);

    // OSC-Dual-State-Pfad (6.1): Property-Updates im Millisekundentakt
    for (int i = 0; i < 100; ++i)
        parameter.setProperty (conduit::id::paramValue, i * 0.01, nullptr);

    CHECK_FALSE (manager.isTopologyDirty());
    manager.flushPendingTopologyUpdate();
    CHECK (manager.getRebuildCount() == 1);    // unverändert
}

//==============================================================================
TEST_CASE ("Preset-Load: Container-Austausch ergibt einen einzigen Rebuild", "[GraphManager]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    conduit::registerDefaultModules (factory);
    conduit::GraphManager manager (root, graph, fader, factory);

    // Geladenes Preset mit eigener Topologie
    auto loaded = makeRootTree();
    auto loadedNodes = loaded.getChildWithName (conduit::id::nodes);
    for (int i = 0; i < 3; ++i)
        loadedNodes.appendChild (makeModuleNode (conduit::AttenuatorModule::staticModuleId), nullptr);

    // Preset-Load-Pfad aus EngineProcessor::setStateInformation():
    // Container werden als ganze Subtrees ersetzt (parent ist der Root)
    root.copyPropertiesAndChildrenFrom (loaded, nullptr);

    REQUIRE (manager.isTopologyDirty());
    manager.flushPendingTopologyUpdate();
    CHECK (manager.getRebuildCount() == 1);
    CHECK (graph.getNumNodes() == 3);
}

//==============================================================================
TEST_CASE ("Graph-Mutationen: Tree-Nodes werden zu Graph-Nodes (add/remove)",
           "[GraphManager]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    conduit::registerDefaultModules (factory);
    conduit::GraphManager manager (root, graph, fader, factory);

    auto nodes = root.getChildWithName (conduit::id::nodes);
    auto attenuator = makeModuleNode (conduit::AttenuatorModule::staticModuleId);
    nodes.appendChild (attenuator, nullptr);

    manager.flushPendingTopologyUpdate();
    CHECK (graph.getNumNodes() == 1);
    CHECK (attenuator.getProperty (conduit::id::nodeError).toString().isEmpty());

    nodes.removeChild (attenuator, nullptr);
    manager.flushPendingTopologyUpdate();
    CHECK (graph.getNumNodes() == 0);
}

//==============================================================================
TEST_CASE ("Graph-Mutationen: Connections werden aus dem Tree synchronisiert",
           "[GraphManager]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    conduit::registerDefaultModules (factory);
    conduit::GraphManager manager (root, graph, fader, factory);

    auto nodes       = root.getChildWithName (conduit::id::nodes);
    auto connections = root.getChildWithName (conduit::id::connections);

    auto source = makeModuleNode (conduit::AttenuatorModule::staticModuleId);
    auto dest   = makeModuleNode (conduit::AttenuatorModule::staticModuleId);
    nodes.appendChild (source, nullptr);
    nodes.appendChild (dest, nullptr);

    // Stereo-Kabel: L→L, R→R
    auto left  = makeConnection (source, 0, dest, 0);
    auto right = makeConnection (source, 1, dest, 1);
    connections.appendChild (left, nullptr);
    connections.appendChild (right, nullptr);

    manager.flushPendingTopologyUpdate();
    CHECK (graph.getNumNodes() == 2);
    CHECK (graph.getConnections().size() == 2u);

    // Kabel ziehen: ein Connection-Child entfernen
    connections.removeChild (right, nullptr);
    manager.flushPendingTopologyUpdate();
    CHECK (graph.getConnections().size() == 1u);

    // Node löschen reißt auch sein verbliebenes Kabel mit
    nodes.removeChild (dest, nullptr);
    manager.flushPendingTopologyUpdate();
    CHECK (graph.getNumNodes() == 1);
    CHECK (graph.getConnections().size() == 0u);
}

//==============================================================================
TEST_CASE ("Async Prepare: unbekannte moduleId → nodeError, kein Graph-Node, kein Retry",
           "[GraphManager]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    conduit::registerDefaultModules (factory);
    conduit::GraphManager manager (root, graph, fader, factory);

    auto nodes = root.getChildWithName (conduit::id::nodes);
    auto unknown = makeModuleNode ("gibts_nicht");
    nodes.appendChild (unknown, nullptr);

    manager.flushPendingTopologyUpdate();
    CHECK (graph.getNumNodes() == 0);
    CHECK (unknown.getProperty (conduit::id::nodeError).toString().isNotEmpty());

    // Kein Retry-Loop: der nächste Swap überspringt den Fehler-Node,
    // gesunde Module sind nicht betroffen
    nodes.appendChild (makeModuleNode (conduit::AttenuatorModule::staticModuleId), nullptr);
    manager.flushPendingTopologyUpdate();
    CHECK (graph.getNumNodes() == 1);
}

//==============================================================================
TEST_CASE ("Async Prepare: fehlschlagendes prepareForGraph → nodeError, Modul nicht im Graph",
           "[GraphManager]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    registerTestModules (factory);
    conduit::GraphManager manager (root, graph, fader, factory);

    auto nodes = root.getChildWithName (conduit::id::nodes);
    auto failing = makeModuleNode ("failing_test_module");
    nodes.appendChild (failing, nullptr);

    manager.flushPendingTopologyUpdate();
    CHECK (graph.getNumNodes() == 0);
    CHECK (failing.getProperty (conduit::id::nodeError).toString()
               == "Allokation fehlgeschlagen (Test)");
}

//==============================================================================
TEST_CASE ("Graph-Swap: vollständiger Fade-Zyklus nach CLAUDE.md 5.2", "[GraphManager]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    fader.prepare (48000.0);  // Audio "läuft" → Swap nur mit Fade-Zyklus
    conduit::ModuleFactory factory;
    conduit::registerDefaultModules (factory);
    conduit::GraphManager manager (root, graph, fader, factory);

    auto nodes = root.getChildWithName (conduit::id::nodes);
    nodes.appendChild (makeModuleNode (conduit::AttenuatorModule::staticModuleId), nullptr);
    REQUIRE (manager.isTopologyDirty());

    // Schritt 1 + 2: erster Loop-Durchlauf prepariert das Modul und startet
    // den Fade-Out — die Topologie wird noch NICHT geändert
    manager.flushPendingTopologyUpdate();
    CHECK (manager.getRebuildCount() == 0);
    CHECK (graph.getNumNodes() == 0);
    CHECK (manager.isWaitingForSilence());
    CHECK (fader.getCurrentPhase() == conduit::GraphFader::Phase::fadingOut);

    // Schritt 3 (Self-Re-Dispatch): solange keine Stille gemeldet ist,
    // findet kein Swap statt
    manager.flushPendingTopologyUpdate();
    CHECK (manager.getRebuildCount() == 0);

    // Coalescing während des Fade-Outs: neue Änderungen landen im selben Swap
    nodes.appendChild (makeModuleNode (conduit::AttenuatorModule::staticModuleId), nullptr);
    nodes.appendChild (makeModuleNode (conduit::AttenuatorModule::staticModuleId), nullptr);

    // Audio Thread rampt auf Stille
    pumpUntilSilent (fader);
    REQUIRE (fader.isFadeOutComplete());

    // Schritt 3: Topologie-Swap auf Stille, dann Schritt 4: Fade-In
    manager.flushPendingTopologyUpdate();
    CHECK (manager.getRebuildCount() == 1);   // 3 Änderungen → 1 Swap
    CHECK (graph.getNumNodes() == 3);         // alle 3 Module im Graph
    CHECK_FALSE (manager.isWaitingForSilence());
    CHECK_FALSE (manager.isTopologyDirty());
    CHECK (fader.getCurrentPhase() == conduit::GraphFader::Phase::fadingIn);
}

//==============================================================================
TEST_CASE ("Graph-Swap: gestopptes Audio während des Fade-Outs blockiert den Swap nicht",
           "[GraphManager]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    fader.prepare (48000.0);
    conduit::ModuleFactory factory;
    conduit::registerDefaultModules (factory);
    conduit::GraphManager manager (root, graph, fader, factory);

    root.getChildWithName (conduit::id::nodes)
        .appendChild (makeModuleNode (conduit::AttenuatorModule::staticModuleId), nullptr);

    manager.flushPendingTopologyUpdate();
    REQUIRE (manager.isWaitingForSilence());

    // Audio stoppt mitten im Fade-Out (EngineProcessor::releaseResources) —
    // der Audio Thread wird nie Stille melden
    fader.reset();

    manager.flushPendingTopologyUpdate();
    CHECK (manager.getRebuildCount() == 1);   // Swap ohne Fade statt Endlos-Dispatch
    CHECK (graph.getNumNodes() == 1);
    CHECK_FALSE (manager.isWaitingForSilence());
}
