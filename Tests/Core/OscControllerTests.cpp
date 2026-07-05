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
TEST_CASE ("OscController: nach Registry-Rebuild ohne den Node erfolgt kein Queue-Push", "[osc]")
{
    OscTestRig rig;
    const auto node = rig.addModuleNodeWithState (conduit::AttenuatorModule::staticModuleId);
    rig.flushAll();
    REQUIRE (rig.osc.getRegisteredAddresses().size() == 1);

    // Node komplett aus dem Tree entfernen → rebuildEndpoints() swappt die
    // Registry unter registryLock. Da der Push denselben Lock nimmt, darf
    // danach kein Update mehr für die alte Adresse in der Audio-Queue landen.
    rig.nodes().removeChild (node, nullptr);
    rig.flushAll();
    REQUIRE (rig.osc.getRegisteredAddresses().isEmpty());

    rig.osc.oscMessageReceived (juce::OSCMessage { gainAddress, 0.5f });
    REQUIRE (rig.audioQueue.getNumReady() == 0);
    REQUIRE_FALSE (rig.osc.isStateDirty());
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

    // Message Thread (dieser Thread): Tree-Updates im UI-Takt anwenden UND
    // die Registry nebenläufig neu aufbauen — deckt den Lock-Scope
    // "Push unter registryLock" gegen den Registry-Swap in rebuildEndpoints()
    // ab (Sanitizer-Ziel: Netzwerk-Push konkurrierend zur Deregistrierung)
    auto mutableNode = node;
    int rebuildToggle = 0;

    while (! networkDone.load (std::memory_order_acquire))
    {
        mutableNode.setProperty (conduit::id::nodeError,
                                 (++rebuildToggle & 1) != 0 ? juce::String ("x")
                                                            : juce::String(),
                                 nullptr);  // nodeError-Change → markRegistryDirty
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

//==============================================================================
// IP-Learn (7.3) — die Loopback-Tests binden echte UDP-Ports und sind
// deshalb hidden ([.], Projektkonvention: Tests/CI bleiben netzwerkfrei);
// lokal explizit via Tag ausführbar: ConduitTests "[network]"

TEST_CASE ("OscController: beginIpLearn ohne Verbindung schlägt fehl", "[osc]")
{
    OscTestRig rig;

    REQUIRE_FALSE (rig.osc.beginIpLearn ([] (const juce::String&) {}));
    REQUIRE_FALSE (rig.osc.isLearning());
}

namespace
{

/** Pumpt Learn-Ergebnisse ohne Message-Loop: der AsyncUpdater wird über
    flushPendingUpdates() synchron abgearbeitet. */
bool pumpUntil (conduit::OscController& osc, const bool& done, int maxMs)
{
    for (int elapsed = 0; elapsed < maxMs && ! done; elapsed += 10)
    {
        juce::Thread::sleep (10);
        osc.flushPendingUpdates();
    }

    return done;
}

} // namespace

TEST_CASE ("OscController: Learn-Probe lernt die Loopback-Absender-IP", "[osc][network][.]")
{
    OscTestRig rig;
    constexpr int port = 58231;
    REQUIRE (rig.osc.connect (port));

    bool fired = false;
    juce::String learned;

    REQUIRE (rig.osc.beginIpLearn ([&] (const juce::String& ip)
    {
        learned = ip;
        fired = true;
    }, 5000));
    REQUIRE (rig.osc.isLearning());

    // Probe Zeit zum Binden geben, dann wiederholt ein Paket schicken
    juce::DatagramSocket sender;

    for (int attempt = 0; attempt < 100 && ! fired; ++attempt)
    {
        sender.write ("127.0.0.1", port, "ping", 4);
        juce::Thread::sleep (20);
        rig.osc.flushPendingUpdates();
    }

    REQUIRE (fired);
    REQUIRE (learned == "127.0.0.1");
    REQUIRE_FALSE (rig.osc.isLearning());
    REQUIRE (rig.osc.getConnectedPort() == port);  // Receiver restauriert

    rig.osc.disconnect();
}

TEST_CASE ("OscController: Learn-Timeout restauriert den Receiver", "[osc][network][.]")
{
    OscTestRig rig;
    constexpr int port = 58233;
    REQUIRE (rig.osc.connect (port));

    bool fired = false;
    juce::String learned { "unset" };

    REQUIRE (rig.osc.beginIpLearn ([&] (const juce::String& ip)
    {
        learned = ip;
        fired = true;
    }, 150));

    REQUIRE (pumpUntil (rig.osc, fired, 5000));
    REQUIRE (learned.isEmpty());  // Timeout → leere IP
    REQUIRE_FALSE (rig.osc.isLearning());
    REQUIRE (rig.osc.getConnectedPort() == port);

    rig.osc.disconnect();
}

TEST_CASE ("OscController: cancelIpLearn restauriert ohne Callback", "[osc][network][.]")
{
    OscTestRig rig;
    constexpr int port = 58235;
    REQUIRE (rig.osc.connect (port));

    bool fired = false;
    REQUIRE (rig.osc.beginIpLearn ([&] (const juce::String&) { fired = true; }, 5000));

    rig.osc.cancelIpLearn();

    REQUIRE_FALSE (rig.osc.isLearning());
    REQUIRE (rig.osc.getConnectedPort() == port);

    // Kein nachlaufender Callback — auch nicht über den AsyncUpdater
    juce::Thread::sleep (50);
    rig.osc.flushPendingUpdates();
    REQUIRE_FALSE (fired);

    rig.osc.disconnect();
}

//==============================================================================
TEST_CASE ("OscAddress (M8): Looper-Aktions-Parser — Adress-Tabelle", "[osc][looper]")
{
    using Action = conduit::osc::LooperOscAction;
    using Type = Action::Type;
    const auto parse = [] (const char* address)
    { return conduit::osc::parseLooperActionAddress (address); };

    // Gueltige Adressen (Indizes 1-basiert -> 0-basiert)
    REQUIRE (parse ("/conduit/looper/stop").type == Type::stopAll);
    REQUIRE (parse ("/conduit/looper/1/commit").type == Type::commit);
    REQUIRE (parse ("/conduit/looper/1/commit").looperIndex == 0);
    REQUIRE (parse ("/conduit/looper/4/stop").type == Type::stopLooper);
    REQUIRE (parse ("/conduit/looper/4/stop").looperIndex == 3);
    REQUIRE (parse ("/conduit/looper/2/target").type == Type::target);

    const auto trackStop = parse ("/conduit/looper/3/track/2/stop");
    REQUIRE (trackStop.type == Type::stopTrack);
    REQUIRE (trackStop.looperIndex == 2);
    REQUIRE (trackStop.trackIndex == 1);

    // Garbage/Grenzen -> none (kein Crash, kein Fallback)
    REQUIRE (parse ("/conduit/looper/0/commit").type == Type::none);
    REQUIRE (parse ("/conduit/looper/5/commit").type == Type::none);
    REQUIRE (parse ("/conduit/looper/x/commit").type == Type::none);
    REQUIRE (parse ("/conduit/looper/1/quatsch").type == Type::none);
    REQUIRE (parse ("/conduit/looper/1/track/9/stop").type == Type::none);
    REQUIRE (parse ("/conduit/looper/1/track/1/start").type == Type::none);
    REQUIRE (parse ("/conduit/looper").type == Type::none);
    REQUIRE (parse ("/conduit/generator/lfo_1/rate").type == Type::none);
}

TEST_CASE ("OscController (M8): Looper-Aktionen — Marshalling auf den MT-Hook", "[osc][looper]")
{
    OscTestRig rig;

    std::vector<conduit::osc::LooperOscAction> received;
    rig.osc.onLooperAction = [&received] (const conduit::osc::LooperOscAction& action)
    { received.push_back (action); };

    using Type = conduit::osc::LooperOscAction::Type;

    // Netzwerk-Thread-Empfang: erst nach dem MT-Drain beim Hook
    rig.osc.oscMessageReceived (juce::OSCMessage { "/conduit/looper/1/commit",
                                                   static_cast<juce::int32> (4) });
    REQUIRE (received.empty());

    rig.osc.flushPendingUpdates();
    REQUIRE (received.size() == 1);
    REQUIRE (received[0].type == Type::commit);
    REQUIRE (received[0].looperIndex == 0);
    REQUIRE (received[0].bars == 4);

    // Float-Toleranz (Max/js) + Target mit Argumenten + Stops
    received.clear();
    rig.osc.oscMessageReceived (juce::OSCMessage { "/conduit/looper/2/commit", 8.0f });
    rig.osc.oscMessageReceived (juce::OSCMessage { "/conduit/looper/2/target",
                                                   static_cast<juce::int32> (3),
                                                   static_cast<juce::int32> (5) });
    rig.osc.oscMessageReceived (juce::OSCMessage { "/conduit/looper/stop" });
    rig.osc.flushPendingUpdates();

    REQUIRE (received.size() == 3);
    REQUIRE (received[0].type == Type::commit);
    REQUIRE (received[0].bars == 8);
    REQUIRE (received[1].type == Type::target);
    REQUIRE (received[1].trackIndex == 2);
    REQUIRE (received[1].slotIndex == 4);
    REQUIRE (received[2].type == Type::stopAll);

    // Ungueltiges wird still verworfen: Commit ohne bars, Target OOB
    received.clear();
    rig.osc.oscMessageReceived (juce::OSCMessage { "/conduit/looper/1/commit" });
    rig.osc.oscMessageReceived (juce::OSCMessage { "/conduit/looper/1/target",
                                                   static_cast<juce::int32> (9),
                                                   static_cast<juce::int32> (1) });
    rig.osc.flushPendingUpdates();
    REQUIRE (received.empty());
}