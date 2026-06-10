#include <catch2/catch_test_macros.hpp>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/GraphManager.h"

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

} // namespace

//==============================================================================
TEST_CASE ("Batch-Coalescing: viele Topologie-Änderungen in einem Frame ergeben einen Rebuild",
           "[GraphManager]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphManager manager (root, graph);

    auto nodes       = root.getChildWithName (conduit::id::nodes);
    auto connections = root.getChildWithName (conduit::id::connections);

    // Bulk-Szenario aus CLAUDE.md 5.5: 5 Module + 20 Kabel in einem Frame
    for (int i = 0; i < 5; ++i)
        nodes.appendChild (juce::ValueTree (conduit::id::node), nullptr);

    for (int i = 0; i < 20; ++i)
        connections.appendChild (juce::ValueTree (conduit::id::connection), nullptr);

    REQUIRE (manager.isTopologyDirty());
    REQUIRE (manager.getRebuildCount() == 0);  // noch kein Loop-Durchlauf

    manager.flushPendingTopologyUpdate();      // = nächster Message-Loop-Durchlauf

    CHECK (manager.getRebuildCount() == 1);    // 25 Änderungen → genau 1 Rebuild
    CHECK_FALSE (manager.isTopologyDirty());

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
    conduit::GraphManager manager (root, graph);

    auto nodes = root.getChildWithName (conduit::id::nodes);
    nodes.appendChild (juce::ValueTree (conduit::id::node), nullptr);
    manager.flushPendingTopologyUpdate();
    REQUIRE (manager.getRebuildCount() == 1);

    // Parameters-Subtree unterhalb eines Nodes ist KEINE Topologie-Änderung
    auto node = nodes.getChild (0);
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
    conduit::GraphManager manager (root, graph);

    // Geladenes Preset mit eigener Topologie
    auto loaded = makeRootTree();
    auto loadedNodes = loaded.getChildWithName (conduit::id::nodes);
    for (int i = 0; i < 3; ++i)
        loadedNodes.appendChild (juce::ValueTree (conduit::id::node), nullptr);

    // Preset-Load-Pfad aus EngineProcessor::setStateInformation():
    // Container werden als ganze Subtrees ersetzt (parent ist der Root)
    root.copyPropertiesAndChildrenFrom (loaded, nullptr);

    REQUIRE (manager.isTopologyDirty());
    manager.flushPendingTopologyUpdate();
    CHECK (manager.getRebuildCount() == 1);
}
