#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "Modules/ModuleFactory.h"
#include "Modules/ScopeModule.h"
#include "UI/ScopeDisplay.h"

using Catch::Approx;

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

void fillWith (juce::AudioBuffer<float>& buffer, float value)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill (buffer.getWritePointer (channel),
                                           value, buffer.getNumSamples());
}

} // namespace

//==============================================================================
TEST_CASE ("ScopeModule: Pass-Through und min/max-Bins über Blockgrenzen", "[scope]")
{
    conduit::ScopeModule scope;
    REQUIRE (scope.prepareForGraph (48000.0, 32).wasOk());

    juce::AudioBuffer<float> buffer (1, 32);
    juce::MidiBuffer midi;

    // binSize 64 = 2 Blöcke à 32: Bin 1 aus zwei 0.25er-Blöcken,
    // Bin 2 aus zwei -0.75er-Blöcken — der Akkumulator muss die
    // Blockgrenze überleben
    fillWith (buffer, 0.25f);
    scope.processBlock (buffer, midi);
    REQUIRE (buffer.getSample (0, 0) == Approx (0.25f));  // Pass-Through unangetastet
    REQUIRE (scope.getScopeQueue().getNumReady() == 0);   // Bin noch nicht voll

    fillWith (buffer, 0.25f);
    scope.processBlock (buffer, midi);

    fillWith (buffer, -0.75f);
    scope.processBlock (buffer, midi);
    fillWith (buffer, -0.75f);
    scope.processBlock (buffer, midi);

    auto& queue = scope.getScopeQueue();
    REQUIRE (queue.getNumReady() == 2);

    conduit::ScopeSample bin;
    REQUIRE (queue.pop (bin));
    REQUIRE (bin.minValue == Approx (0.25f));
    REQUIRE (bin.maxValue == Approx (0.25f));

    REQUIRE (queue.pop (bin));
    REQUIRE (bin.minValue == Approx (-0.75f));
    REQUIRE (bin.maxValue == Approx (-0.75f));
}

//==============================================================================
TEST_CASE ("ScopeModule: volle Queue blockiert den Audio Thread nicht", "[scope]")
{
    conduit::ScopeModule scope;
    REQUIRE (scope.prepareForGraph (48000.0, 512).wasOk());

    juce::AudioBuffer<float> buffer (1, 512);
    juce::MidiBuffer midi;

    // 4096er-Queue überfüllen: 1200 Blöcke à 512 Samples = 9600 Bins
    for (int block = 0; block < 1200; ++block)
    {
        fillWith (buffer, 0.5f);
        scope.processBlock (buffer, midi);  // Drops statt Block — kein Hängen
    }

    REQUIRE (scope.getScopeQueue().getNumReady() == scope.getScopeQueue().getCapacity());
}

//==============================================================================
TEST_CASE ("ScopeDisplay: zieht Bins über transienten Modul-Lookup (5.3)", "[scope]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };
    conduit::registerDefaultModules (factory);

    const auto node = manager.addModuleNode (conduit::ScopeModule::staticModuleId, {});
    REQUIRE (node.isValid());
    manager.flushPendingTopologyUpdate();

    const auto nodeUuid = node.getProperty (conduit::id::nodeId).toString();
    auto* scope = dynamic_cast<conduit::ScopeModule*> (manager.getModuleFor (nodeUuid));
    REQUIRE (scope != nullptr);

    conduit::ScopeDisplay display (manager, nodeUuid);
    display.setSize (200, 100);

    // 2 Bins produzieren, Display zieht sie ab
    juce::AudioBuffer<float> buffer (1, 128);
    juce::MidiBuffer midi;
    fillWith (buffer, 0.5f);
    scope->processBlock (buffer, midi);

    REQUIRE (scope->getScopeQueue().getNumReady() == 2);
    display.pullPendingSamples();
    REQUIRE (scope->getScopeQueue().getNumReady() == 0);

    // Nach dem Delete (Phase 2) läuft der Lookup ins Leere — kein Crash
    REQUIRE (manager.requestNodeDelete (nodeUuid));
    manager.flushPendingTopologyUpdate();
    REQUIRE (manager.getModuleFor (nodeUuid) == nullptr);
    display.pullPendingSamples();  // no-op
}
