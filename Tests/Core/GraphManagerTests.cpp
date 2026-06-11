#include <catch2/catch_test_macros.hpp>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/GraphFader.h"
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

} // namespace

//==============================================================================
TEST_CASE ("Batch-Coalescing: viele Topologie-Änderungen in einem Frame ergeben einen Rebuild",
           "[GraphManager]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;  // unprepared → Swap ohne Fade
    conduit::GraphManager manager (root, graph, fader);

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
    conduit::GraphFader fader;
    conduit::GraphManager manager (root, graph, fader);

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
    conduit::GraphFader fader;
    conduit::GraphManager manager (root, graph, fader);

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

//==============================================================================
TEST_CASE ("Graph-Swap: vollständiger Fade-Zyklus nach CLAUDE.md 5.2", "[GraphManager]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    fader.prepare (48000.0);  // Audio "läuft" → Swap nur mit Fade-Zyklus
    conduit::GraphManager manager (root, graph, fader);

    auto nodes = root.getChildWithName (conduit::id::nodes);
    nodes.appendChild (juce::ValueTree (conduit::id::node), nullptr);
    REQUIRE (manager.isTopologyDirty());

    // Schritt 2: erster Loop-Durchlauf startet den Fade-Out —
    // die Topologie wird noch NICHT geändert
    manager.flushPendingTopologyUpdate();
    CHECK (manager.getRebuildCount() == 0);
    CHECK (manager.isWaitingForSilence());
    CHECK (fader.getCurrentPhase() == conduit::GraphFader::Phase::fadingOut);

    // Schritt 3 (Self-Re-Dispatch): solange keine Stille gemeldet ist,
    // findet kein Swap statt
    manager.flushPendingTopologyUpdate();
    CHECK (manager.getRebuildCount() == 0);

    // Coalescing während des Fade-Outs: neue Änderungen landen im selben Swap
    nodes.appendChild (juce::ValueTree (conduit::id::node), nullptr);
    nodes.appendChild (juce::ValueTree (conduit::id::node), nullptr);

    // Audio Thread rampt auf Stille
    pumpUntilSilent (fader);
    REQUIRE (fader.isFadeOutComplete());

    // Schritt 3: Topologie-Swap auf Stille, dann Schritt 4: Fade-In
    manager.flushPendingTopologyUpdate();
    CHECK (manager.getRebuildCount() == 1);   // 3 Änderungen → 1 Swap
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
    conduit::GraphManager manager (root, graph, fader);

    root.getChildWithName (conduit::id::nodes)
        .appendChild (juce::ValueTree (conduit::id::node), nullptr);

    manager.flushPendingTopologyUpdate();
    REQUIRE (manager.isWaitingForSilence());

    // Audio stoppt mitten im Fade-Out (EngineProcessor::releaseResources) —
    // der Audio Thread wird nie Stille melden
    fader.reset();

    manager.flushPendingTopologyUpdate();
    CHECK (manager.getRebuildCount() == 1);   // Swap ohne Fade statt Endlos-Dispatch
    CHECK_FALSE (manager.isWaitingForSilence());
}
