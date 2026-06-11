#include <atomic>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "Core/OscController.h"
#include "Modules/AttenuatorModule.h"
#include "Modules/ModuleFactory.h"

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

// Gemeinsames Setup: OscController mit GraphManager-Kette und Audio-Queue
struct OscTestRig
{
    OscTestRig()
    {
        conduit::registerDefaultModules (factory);
    }

    [[nodiscard]] juce::ValueTree nodes() { return root.getChildWithName (conduit::id::nodes); }

    /** Voller Modul-Lifecycle wie in der App (4.4): createState() VOR dem
        Einhängen — der Subtree trägt damit type + Parameters[]. */
    juce::ValueTree addModuleNodeWithState (const juce::String& moduleId)
    {
        auto module = factory.create (moduleId);
        REQUIRE (module != nullptr);

        auto node = module->createState();
        nodes().appendChild (node, nullptr);
        return node;
    }

    /** Ein Message-Loop-Durchlauf: GraphManager materialisiert (Fader
        unprepared → Direkt-Swap), dann löst der OscController auf. */
    void flushAll()
    {
        manager.flushPendingTopologyUpdate();
        osc.flushPendingUpdates();
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    juce::ValueTree root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };
    conduit::SpscQueue<conduit::ParameterUpdate> audioQueue { 64 };
    conduit::OscController osc { root, manager, audioQueue };
};

constexpr auto gainAddress = "/conduit/utility/attenuator/gain";

} // namespace

//==============================================================================
TEST_CASE ("OscController: Auto-Registration via ValueTree::Listener (7.1)", "[osc]")
{
    OscTestRig rig;
    REQUIRE (rig.osc.getRegisteredAddresses().isEmpty());

    rig.addModuleNodeWithState (conduit::AttenuatorModule::staticModuleId);
    rig.flushAll();

    const auto addresses = rig.osc.getRegisteredAddresses();
    REQUIRE (addresses.size() == 1);
    REQUIRE (addresses[0] == gainAddress);
}

//==============================================================================
TEST_CASE ("OscController: Dual-State-Dispatch — Queue sofort, ValueTree async (6.1)", "[osc]")
{
    OscTestRig rig;
    const auto node = rig.addModuleNodeWithState (conduit::AttenuatorModule::staticModuleId);
    rig.flushAll();

    juce::OSCMessage message { gainAddress, 0.5f };
    rig.osc.oscMessageReceived (message);  // simulierter Netzwerk-Thread-Empfang

    // Pfad 1: Update liegt sofort in der Audio-Queue, mit aufgelöstem Target
    conduit::ParameterUpdate update;
    REQUIRE (rig.audioQueue.pop (update));
    REQUIRE (update.target != nullptr);
    REQUIRE (juce::exactlyEqual (update.value, 0.5f));

    // Pfad 2: Tree folgt erst nach dem Message-Loop-Durchlauf
    REQUIRE (rig.osc.isStateDirty());
    rig.osc.flushPendingUpdates();

    const auto parameter = node.getChildWithName (conduit::id::parameters)
                               .getChildWithProperty (conduit::id::paramId, "gain");
    REQUIRE (juce::exactlyEqual ((float) (double) parameter.getProperty (conduit::id::paramValue), 0.5f));
    REQUIRE_FALSE (rig.osc.isStateDirty());

    // Target schreibt wirklich in das live-Modul
    update.target->store (0.25f, std::memory_order_relaxed);
    auto* module = rig.manager.getModuleFor (node.getProperty (conduit::id::nodeId).toString());
    REQUIRE (module != nullptr);
    REQUIRE (module->getParameterTarget ("gain") == update.target);
}

//==============================================================================
TEST_CASE ("OscController: Werte werden auf [min, max] geclamped", "[osc]")
{
    OscTestRig rig;
    const auto node = rig.addModuleNodeWithState (conduit::AttenuatorModule::staticModuleId);
    rig.flushAll();

    rig.osc.oscMessageReceived (juce::OSCMessage { gainAddress, 2.0f });

    conduit::ParameterUpdate update;
    REQUIRE (rig.audioQueue.pop (update));
    REQUIRE (juce::exactlyEqual (update.value, 1.0f));  // gain-Range ist [0, 1]

    rig.osc.flushPendingUpdates();
    const auto parameter = node.getChildWithName (conduit::id::parameters)
                               .getChildWithProperty (conduit::id::paramId, "gain");
    REQUIRE (juce::exactlyEqual ((float) (double) parameter.getProperty (conduit::id::paramValue), 1.0f));
}

//==============================================================================
TEST_CASE ("OscController: Phase-1-Delete deregistriert sofort (5.3 / 7.1)", "[osc]")
{
    OscTestRig rig;
    const auto node = rig.addModuleNodeWithState (conduit::AttenuatorModule::staticModuleId);
    rig.flushAll();
    REQUIRE (rig.osc.getRegisteredAddresses().size() == 1);

    // Phase 1: nodeState → Deleting. Der Subtree ist noch im Tree —
    // die Adressen müssen trotzdem sofort verschwinden.
    const auto nodeUuid = node.getProperty (conduit::id::nodeId).toString();
    REQUIRE (rig.manager.requestNodeDelete (nodeUuid));
    rig.osc.flushPendingUpdates();

    REQUIRE (rig.nodes().getNumChildren() == 1);  // Phase 2 lief noch nicht
    REQUIRE (rig.osc.getRegisteredAddresses().isEmpty());

    // Messages an die alte Adresse verpuffen folgenlos
    rig.osc.oscMessageReceived (juce::OSCMessage { gainAddress, 0.5f });
    conduit::ParameterUpdate update;
    REQUIRE_FALSE (rig.audioQueue.pop (update));
    REQUIRE_FALSE (rig.osc.isStateDirty());

    // Phase 2 (Subtree-Entfernung) läuft ohne Crash durch
    rig.flushAll();
    REQUIRE (rig.nodes().getNumChildren() == 0);
}

//==============================================================================
TEST_CASE ("OscController: unbekannte Adressen und falsche Argument-Typen werden ignoriert", "[osc]")
{
    OscTestRig rig;
    rig.addModuleNodeWithState (conduit::AttenuatorModule::staticModuleId);
    rig.flushAll();

    rig.osc.oscMessageReceived (juce::OSCMessage { "/conduit/utility/unbekannt/gain", 0.5f });
    rig.osc.oscMessageReceived (juce::OSCMessage { gainAddress, juce::String ("kein float") });
    rig.osc.oscMessageReceived (juce::OSCMessage { gainAddress });  // ohne Argument

    conduit::ParameterUpdate update;
    REQUIRE_FALSE (rig.audioQueue.pop (update));
    REQUIRE_FALSE (rig.osc.isStateDirty());

    // int32 wird dagegen akzeptiert und konvertiert
    rig.osc.oscMessageReceived (juce::OSCMessage { gainAddress, static_cast<juce::int32> (1) });
    REQUIRE (rig.audioQueue.pop (update));
    REQUIRE (juce::exactlyEqual (update.value, 1.0f));
}

//==============================================================================
TEST_CASE ("OscController: Empfang nebenläufig zu Audio-Drain und Tree-Flush",
           "[osc][threading]")
{
    OscTestRig rig;
    const auto node = rig.addModuleNodeWithState (conduit::AttenuatorModule::staticModuleId);
    rig.flushAll();

    auto* module = dynamic_cast<conduit::AttenuatorModule*> (
        rig.manager.getModuleFor (node.getProperty (conduit::id::nodeId).toString()));
    REQUIRE (module != nullptr);

    constexpr int messageCount = 20'000;
    std::atomic<bool> networkDone { false };

    // Simulierter Netzwerk-Thread: OSC-Messages im Dauerfeuer (6.1)
    std::thread networkThread ([&rig, &networkDone]
    {
        for (int i = 0; i < messageCount; ++i)
            rig.osc.oscMessageReceived (
                juce::OSCMessage { gainAddress, static_cast<float> (i % 101) * 0.01f });

        networkDone.store (true, std::memory_order_release);
    });

    // Simulierter Audio Thread: Queue dränieren + processBlock (wie der
    // EngineProcessor); volle Queue drosselt nur den Echtzeit-Pfad
    std::thread audioThread ([&rig, &networkDone, module]
    {
        juce::AudioBuffer<float> buffer (2, 32);
        juce::MidiBuffer midi;
        conduit::ParameterUpdate update;

        while (! networkDone.load (std::memory_order_acquire)
               || rig.audioQueue.getNumReady() > 0)
        {
            while (rig.audioQueue.pop (update))
                if (update.target != nullptr)
                    update.target->store (update.value, std::memory_order_relaxed);

            buffer.clear();
            module->processBlock (buffer, midi);
        }
    });

    // Message Thread (dieser Thread): Tree-Updates im UI-Takt anwenden
    while (! networkDone.load (std::memory_order_acquire))
    {
        rig.osc.flushPendingUpdates();
        std::this_thread::yield();
    }

    networkThread.join();
    audioThread.join();

    // Finale Konsistenz: letzter Wert steht im Tree, nichts hängt mehr
    rig.osc.flushPendingUpdates();
    REQUIRE_FALSE (rig.osc.isStateDirty());
    REQUIRE (rig.audioQueue.getNumReady() == 0);

    const auto parameter = node.getChildWithName (conduit::id::parameters)
                               .getChildWithProperty (conduit::id::paramId, "gain");
    const auto treeValue = (float) (double) parameter.getProperty (conduit::id::paramValue);
    REQUIRE (treeValue >= 0.0f);
    REQUIRE (treeValue <= 1.0f);
}
