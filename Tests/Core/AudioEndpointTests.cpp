#include <catch2/catch_test_macros.hpp>

#include "Core/EngineProcessor.h"
#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "Modules/AttenuatorModule.h"
#include "Modules/AudioEndpointModule.h"
#include "Modules/ModuleFactory.h"
#include "TestSettingsFolder.h"

namespace
{

using namespace conduit;

juce::ValueTree makeRootTree()
{
    juce::ValueTree root (id::root);
    root.appendChild (juce::ValueTree (id::nodes),               nullptr);
    root.appendChild (juce::ValueTree (id::connections),         nullptr);
    root.appendChild (juce::ValueTree (id::calibrationProfiles), nullptr);
    return root;
}

juce::String uuidOf (const juce::ValueTree& node)
{
    return node.getProperty (id::nodeId).toString();
}

juce::File tempPresetFile (const juce::String& name)
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile (name + EngineProcessor::presetFileExtension);
}

/** GraphManager-Rig (Muster NodeCanvasTests) — headless, ohne UI. */
struct EndpointRig
{
    EndpointRig() { registerDefaultModules (factory); }

    [[nodiscard]] juce::ValueTree nodes()       { return root.getChildWithName (id::nodes); }
    [[nodiscard]] juce::ValueTree connections() { return root.getChildWithName (id::connections); }

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
TEST_CASE ("AudioEndpoint: I/O-Module sind im Browser registriert (ADR 009)", "[io][endpoint]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::ModuleFactory factory;
    registerDefaultModules (factory);

    const auto input = factory.getDescriptor (audioInputModuleId);
    CHECK (input.id == juce::String (audioInputModuleId));
    CHECK (input.category == "I/O");

    const auto output = factory.getDescriptor (audioOutputModuleId);
    CHECK (output.id == juce::String (audioOutputModuleId));

    // createState liefert die Port-Sicht des Schemas 6.2
    const auto inState = factory.create (audioInputModuleId)->createState();
    CHECK ((int) inState.getProperty (id::numInputChannels) == 0);
    CHECK ((int) inState.getProperty (id::numOutputChannels) == 2);

    const auto outState = factory.create (audioOutputModuleId)->createState();
    CHECK ((int) outState.getProperty (id::numInputChannels) == 2);
    CHECK ((int) outState.getProperty (id::numOutputChannels) == 0);
}

TEST_CASE ("AudioEndpoint: Materialisierung + implizite Anker-Kabel", "[io][endpoint]")
{
    EndpointRig rig;

    // Hardware-Anker (in der App: die AudioGraphIOProcessor)
    const auto anchorIn  = rig.graph.addNode (std::make_unique<AttenuatorModule>())->nodeID;
    const auto anchorOut = rig.graph.addNode (std::make_unique<AttenuatorModule>())->nodeID;
    rig.manager.registerExternalEndpoint (audioInputModuleId,  anchorIn);
    rig.manager.registerExternalEndpoint (audioOutputModuleId, anchorOut);

    const auto inNode  = rig.manager.addModuleNode (audioInputModuleId,  { 40, 100 });
    const auto outNode = rig.manager.addModuleNode (audioOutputModuleId, { 600, 100 });
    REQUIRE (inNode.isValid());
    REQUIRE (outNode.isValid());

    rig.manager.flushPendingTopologyUpdate();

    // Beide Module sind ECHTE Graph-Prozessoren (keine Reserved-Mappings)
    auto* inModule  = rig.manager.getModuleFor (uuidOf (inNode));
    auto* outModule = rig.manager.getModuleFor (uuidOf (outNode));
    REQUIRE (inModule != nullptr);
    REQUIRE (outModule != nullptr);
    CHECK (dynamic_cast<AudioEndpointModule*> (inModule) != nullptr);

    // Anker-Kabel: anchor → input-Proxy bzw. output-Proxy → anchor (2 Kanäle)
    const auto isConnected = [&rig] (juce::AudioProcessorGraph::NodeID src, ConduitModule* dstModule,
                                     bool proxyIsSource)
    {
        int found = 0;
        for (const auto& c : rig.graph.getConnections())
        {
            auto* proxyNode = rig.graph.getNodeForId (proxyIsSource ? c.source.nodeID
                                                                    : c.destination.nodeID);
            const auto other = proxyIsSource ? c.destination.nodeID : c.source.nodeID;

            if (proxyNode != nullptr && proxyNode->getProcessor() == dstModule && other == src)
                ++found;
        }
        return found;
    };

    CHECK (isConnected (anchorIn,  inModule,  false) == 2);   // anchor → proxy
    CHECK (isConnected (anchorOut, outModule, true)  == 2);   // proxy → anchor

    // Patch-Kabel input-Proxy → output-Proxy laufen über den normalen Pfad
    REQUIRE (rig.manager.addConnection (uuidOf (inNode), 0, uuidOf (outNode), 0));
    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.connections().getNumChildren() == 1);
}

TEST_CASE ("AudioEndpoint: Mehrfach-Outs summieren auf denselben Anker (ADR 009)", "[io][endpoint]")
{
    EndpointRig rig;

    const auto anchorOut = rig.graph.addNode (std::make_unique<AttenuatorModule>())->nodeID;
    rig.manager.registerExternalEndpoint (audioOutputModuleId, anchorOut);

    const auto outA = rig.manager.addModuleNode (audioOutputModuleId, { 600, 100 });
    const auto outB = rig.manager.addModuleNode (audioOutputModuleId, { 600, 300 });
    const auto lfo  = rig.manager.addModuleNode ("attenuator", { 100, 100 });
    REQUIRE (outA.isValid());
    REQUIRE (outB.isValid());

    // Dieselbe Quelle auf BEIDE Outs, gleiche Kanäle — der Graph summiert
    REQUIRE (rig.manager.addConnection (uuidOf (lfo), 0, uuidOf (outA), 0));
    REQUIRE (rig.manager.addConnection (uuidOf (lfo), 0, uuidOf (outB), 0));

    rig.manager.flushPendingTopologyUpdate();

    REQUIRE (rig.manager.getModuleFor (uuidOf (outA)) != nullptr);
    REQUIRE (rig.manager.getModuleFor (uuidOf (outB)) != nullptr);
    CHECK (rig.connections().getNumChildren() == 2);

    // Beide Anker-Verkabelungen auf denselben Ziel-Pin existieren im Graph
    int anchorFeeds = 0;
    for (const auto& c : rig.graph.getConnections())
        if (c.destination.nodeID == anchorOut && c.destination.channelIndex == 0)
            ++anchorFeeds;
    CHECK (anchorFeeds == 2);
}

TEST_CASE ("AudioEndpoint: Delete entfernt Modul + Kabel, EIN Undo stellt beides her", "[io][endpoint]")
{
    EndpointRig rig;

    const auto outNode = rig.manager.addModuleNode (audioOutputModuleId, { 600, 100 });
    const auto source  = rig.manager.addModuleNode ("attenuator", { 100, 100 });
    REQUIRE (rig.manager.addConnection (uuidOf (source), 0, uuidOf (outNode), 0));
    REQUIRE (rig.manager.addConnection (uuidOf (source), 1, uuidOf (outNode), 1));
    rig.manager.flushPendingTopologyUpdate();

    REQUIRE (rig.nodes().getNumChildren() == 2);
    REQUIRE (rig.connections().getNumChildren() == 2);

    // Reserved-Sperre ist Geschichte: requestNodeDelete akzeptiert I/O
    REQUIRE (rig.manager.requestNodeDelete (uuidOf (outNode)));
    rig.manager.flushPendingTopologyUpdate();   // Phase 2 (keine UI-Referenzen)

    CHECK (rig.nodes().getNumChildren() == 1);
    CHECK (rig.connections().getNumChildren() == 0);

    // EIN Undo: Modul UND beide Kabel zurück (eine Transaktion)
    REQUIRE (rig.undoManager.undo());
    CHECK (rig.nodes().getNumChildren() == 2);
    CHECK (rig.connections().getNumChildren() == 2);
}

//==============================================================================
TEST_CASE ("AudioEndpoint-Migration: Alt-Patch erhält Default-I/O + Version 3", "[io][endpoint]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;

    // Legacy-Root (V1): ganz ohne I/O-Nodes
    const auto file = tempPresetFile ("conduit_io_migration");
    {
        const auto xml = makeRootTree().createXml();
        REQUIRE (xml->writeTo (file));
    }

    EngineProcessor engine { settingsFolder.folder };
    REQUIRE (engine.loadPreset (file).wasOk());

    const auto root  = engine.getRootState();
    const auto nodes = root.getChildWithName (id::nodes);

    CHECK (nodes.getChildWithProperty (id::factoryId,
                                       juce::String (audioInputModuleId)).isValid());
    CHECK (nodes.getChildWithProperty (id::factoryId,
                                       juce::String (audioOutputModuleId)).isValid());
    CHECK ((int) root.getProperty (id::rootStateVersion) == EngineProcessor::ioRootVersion);

    // Materialisierbar als echte Module (kein Reserved-Mapping mehr)
    engine.getGraphManager().flushPendingTopologyUpdate();
    const auto inNode = nodes.getChildWithProperty (id::factoryId,
                                                    juce::String (audioInputModuleId));
    CHECK (dynamic_cast<AudioEndpointModule*> (
               engine.getGraphManager().getModuleFor (uuidOf (inNode))) != nullptr);
}

TEST_CASE ("AudioEndpoint-Migration: V3-Patch ohne Output bleibt ohne (kein Auto-Repair)", "[io][endpoint]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    const auto file = tempPresetFile ("conduit_io_no_repair");

    // V3-Patch bauen, Output löschen, speichern
    {
        EngineProcessor engine { settingsFolder.folder };
        auto nodes = engine.getRootState().getChildWithName (id::nodes);
        const auto outNode = nodes.getChildWithProperty (id::factoryId,
                                                         juce::String (audioOutputModuleId));
        REQUIRE (outNode.isValid());
        REQUIRE (engine.getGraphManager().requestNodeDelete (uuidOf (outNode)));
        engine.getGraphManager().flushPendingTopologyUpdate();
        REQUIRE_FALSE (nodes.getChildWithProperty (id::factoryId,
                                                   juce::String (audioOutputModuleId)).isValid());
        REQUIRE (engine.savePreset (file).wasOk());
    }

    // Laden: der gelöschte Output kommt NICHT zurück (Stille ist gewollt)
    EngineProcessor engine { settingsFolder.folder };
    REQUIRE (engine.loadPreset (file).wasOk());

    const auto nodes = engine.getRootState().getChildWithName (id::nodes);
    CHECK_FALSE (nodes.getChildWithProperty (id::factoryId,
                                             juce::String (audioOutputModuleId)).isValid());
    CHECK (nodes.getChildWithProperty (id::factoryId,
                                       juce::String (audioInputModuleId)).isValid());
}
