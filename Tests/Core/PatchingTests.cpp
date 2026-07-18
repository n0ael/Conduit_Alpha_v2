#include <catch2/catch_test_macros.hpp>

#include "Core/EngineProcessor.h"
#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "Modules/AttenuatorModule.h"
#include "Modules/ModuleFactory.h"
#include "TestSettingsFolder.h"

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

constexpr auto attenuatorId = conduit::AttenuatorModule::staticModuleId;

juce::String uuidOf (const juce::ValueTree& node)
{
    return node.getProperty (conduit::id::nodeId).toString();
}

struct PatchRig
{
    PatchRig()
    {
        conduit::registerDefaultModules (factory);
    }

    [[nodiscard]] juce::ValueTree nodes()       { return root.getChildWithName (conduit::id::nodes); }
    [[nodiscard]] juce::ValueTree connections() { return root.getChildWithName (conduit::id::connections); }

    /** Stellvertreter für einen extern verwalteten Graph-Node (in der App:
        die AudioGraphIOProcessor des EngineProcessor). */
    juce::AudioProcessorGraph::NodeID addExternalGraphNode()
    {
        return graph.addNode (std::make_unique<conduit::AttenuatorModule>())->nodeID;
    }

    /** Tree-Node für einen externen Endpunkt — wie EngineProcessor::ensureIONodeStates. */
    juce::ValueTree addIONodeState (const char* moduleId, int numInputs, int numOutputs)
    {
        juce::ValueTree node (conduit::id::node);
        node.setProperty (conduit::id::nodeId, juce::Uuid().toString(), nullptr);
        node.setProperty (conduit::id::type, toString (conduit::ModuleType::io), nullptr);
        node.setProperty (conduit::id::moduleId, moduleId, nullptr);
        node.setProperty (conduit::id::stateVersion, 1, nullptr);
        node.setProperty (conduit::id::nodeState, toString (conduit::NodeState::active), nullptr);
        node.setProperty (conduit::id::nodeError, juce::String(), nullptr);
        node.setProperty (conduit::id::numInputChannels, numInputs, nullptr);
        node.setProperty (conduit::id::numOutputChannels, numOutputs, nullptr);
        node.appendChild (juce::ValueTree (conduit::id::parameters), nullptr);
        nodes().appendChild (node, nullptr);
        return node;
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    juce::ValueTree root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };
};

} // namespace

//==============================================================================
TEST_CASE ("I/O-Endpunkte (ADR 009): Proxy-Materialisierung + implizite Anker-Kabel", "[patching]")
{
    PatchRig rig;
    rig.manager.registerExternalEndpoint (conduit::audioInputModuleId,  rig.addExternalGraphNode());
    rig.manager.registerExternalEndpoint (conduit::audioOutputModuleId, rig.addExternalGraphNode());

    const auto ioIn  = rig.addIONodeState (conduit::audioInputModuleId,  0, 2);
    const auto ioOut = rig.addIONodeState (conduit::audioOutputModuleId, 2, 0);
    const auto att   = rig.manager.addModuleNode (attenuatorId, {});

    rig.manager.flushPendingTopologyUpdate();

    // Reguläre Factory-Materialisierung: 2 Anker + 2 Proxys + 1 Attenuator;
    // Anker-Kabel implizit (2 Kanäle je Endpunkt)
    REQUIRE (ioIn.getProperty (conduit::id::nodeError).toString().isEmpty());
    REQUIRE (ioOut.getProperty (conduit::id::nodeError).toString().isEmpty());
    REQUIRE (rig.graph.getNumNodes() == 5);
    REQUIRE (rig.graph.getConnections().size() == 4);

    // Kabelzug In → Attenuator → Out landet zusätzlich im Graph
    REQUIRE (rig.manager.addConnection (uuidOf (ioIn), 0, uuidOf (att), 0));
    REQUIRE (rig.manager.addConnection (uuidOf (att), 0, uuidOf (ioOut), 0));
    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.graph.getConnections().size() == 6);

    // Modul-Delete entfernt nur das Modul — Proxys und Anker bleiben
    REQUIRE (rig.manager.requestNodeDelete (uuidOf (att)));
    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.graph.getNumNodes() == 4);

    // I/O ist regulär löschbar (ADR 009) — der ANKER bleibt im Graph
    REQUIRE (rig.manager.requestNodeDelete (uuidOf (ioIn)));
    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.graph.getNumNodes() == 3);   // in-Proxy weg, beide Anker + out-Proxy

    // Tree-Node verschwindet ohne Delete-Phase (Preset) → Proxy weg, Anker bleibt
    rig.nodes().removeChild (ioOut, nullptr);
    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.graph.getNumNodes() == 2);   // nur noch die beiden Anker
}

//==============================================================================
TEST_CASE ("addConnection validiert: Duplikat, Selbstverbindung, unbekannte Endpunkte", "[patching]")
{
    PatchRig rig;
    const auto a = rig.manager.addModuleNode (attenuatorId, {});
    const auto b = rig.manager.addModuleNode (attenuatorId, {});

    REQUIRE (rig.manager.addConnection (uuidOf (a), 0, uuidOf (b), 0));
    REQUIRE_FALSE (rig.manager.addConnection (uuidOf (a), 0, uuidOf (b), 0));   // Duplikat
    REQUIRE_FALSE (rig.manager.addConnection (uuidOf (a), 0, uuidOf (a), 1));   // Selbstverbindung
    REQUIRE_FALSE (rig.manager.addConnection ("nicht-da", 0, uuidOf (b), 1));   // unbekannt

    REQUIRE (rig.connections().getNumChildren() == 1);
}

//==============================================================================
TEST_CASE ("removeConnection ist undo-fähig und erreicht den Graph", "[patching]")
{
    PatchRig rig;
    const auto a = rig.manager.addModuleNode (attenuatorId, {});
    const auto b = rig.manager.addModuleNode (attenuatorId, {});
    REQUIRE (rig.manager.addConnection (uuidOf (a), 0, uuidOf (b), 0));
    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.graph.getConnections().size() == 1);

    REQUIRE (rig.manager.removeConnection (uuidOf (a), 0, uuidOf (b), 0));
    REQUIRE_FALSE (rig.manager.removeConnection (uuidOf (a), 0, uuidOf (b), 0));  // schon weg
    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.graph.getConnections().empty());

    REQUIRE (rig.undoManager.undo());
    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.graph.getConnections().size() == 1);
}

//==============================================================================
TEST_CASE ("EngineProcessor: erster hörbarer Patch — Audio In → Attenuator → Audio Out",
           "[patching]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };
    auto& manager = engine.getGraphManager();

    // I/O-Grundausstattung steht im frischen Patch
    auto nodes = engine.getRootState().getChildWithName (conduit::id::nodes);
    const auto ioIn  = nodes.getChildWithProperty (conduit::id::factoryId,
                                                   juce::String (conduit::audioInputModuleId));
    const auto ioOut = nodes.getChildWithProperty (conduit::id::factoryId,
                                                   juce::String (conduit::audioOutputModuleId));
    REQUIRE (ioIn.isValid());
    REQUIRE (ioOut.isValid());

    engine.prepareToPlay (48000.0, 32);  // Audio läuft → Swap nutzt den Fade-Zyklus

    const auto att = manager.addModuleNode (attenuatorId, { 300, 200 });
    REQUIRE (att.isValid());

    for (int channel = 0; channel < 2; ++channel)
    {
        REQUIRE (manager.addConnection (uuidOf (ioIn), channel, uuidOf (att), channel));
        REQUIRE (manager.addConnection (uuidOf (att), channel, uuidOf (ioOut), channel));
    }

    juce::AudioBuffer<float> buffer (2, 32);
    juce::MidiBuffer midi;

    const auto runBlocks = [&] (int count)
    {
        for (int i = 0; i < count; ++i)
        {
            for (int channel = 0; channel < 2; ++channel)
                juce::FloatVectorOperations::fill (buffer.getWritePointer (channel), 1.0f, 32);

            engine.processBlock (buffer, midi);
        }
    };

    // Fade-Swap mit laufendem Audio durchpumpen (5.2 Schritt 2–4)
    manager.flushPendingTopologyUpdate();

    for (int i = 0; i < 100 && manager.isWaitingForSilence(); ++i)
    {
        runBlocks (1);
        manager.flushPendingTopologyUpdate();
    }

    REQUIRE_FALSE (manager.isWaitingForSilence());

    runBlocks (12);  // Fade-In ausklingen lassen
    REQUIRE (buffer.getMagnitude (0, 32) > 0.9f);  // Signal kommt durch (gain 1.0)

    // UI-Pfad: gain im Tree → Parameter-Sync → hörbar leiser
    att.getChildWithName (conduit::id::parameters)
       .getChildWithProperty (conduit::id::paramId, "gain")
       .setProperty (conduit::id::paramValue, 0.25, nullptr);

    runBlocks (12);  // SmoothedValue (5ms = 240 Samples) ausklingen lassen
    REQUIRE (buffer.getMagnitude (0, 32) < 0.3f);
    REQUIRE (buffer.getMagnitude (0, 32) > 0.2f);
}
