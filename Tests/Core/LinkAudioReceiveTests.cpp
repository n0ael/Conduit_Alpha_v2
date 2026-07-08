#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/LinkClock.h"
#include "Core/NodeUiRegistry.h"
#include "Modules/LinkAudioReceiveModule.h"
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

juce::String uuidOf (const juce::ValueTree& nodeTree)
{
    return nodeTree.getProperty (conduit::id::nodeId).toString();
}

} // namespace

//==============================================================================
TEST_CASE ("LinkAudioReceiveModule: createState-Schema (6.2) + Parameter-Ziel", "[linkaudio][receive]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LinkAudioReceiveModule module;

    auto tree = module.createState();
    REQUIRE (tree.getProperty (conduit::id::factoryId).toString()
             == juce::String (conduit::LinkAudioReceiveModule::staticModuleId));
    REQUIRE (static_cast<int> (tree.getProperty (conduit::id::numInputChannels)) == 0);
    REQUIRE (static_cast<int> (tree.getProperty (conduit::id::numOutputChannels)) == 2);

    // Kanal-Wunsch als Namen — vorhanden, leer (nie ChannelKeys, CLAUDE.md 6)
    REQUIRE (tree.hasProperty (conduit::id::targetPeer));
    REQUIRE (tree.hasProperty (conduit::id::targetChannel));
    REQUIRE (tree.getProperty (conduit::id::targetPeer).toString().isEmpty());

    // latency_ms nach Schema (Wert/Min/Max/Default)
    const auto params = tree.getChildWithName (conduit::id::parameters);
    const auto latency = params.getChildWithProperty (
        conduit::id::paramId, juce::String (conduit::LinkAudioReceiveModule::latencyParamId));
    REQUIRE (latency.isValid());
    REQUIRE (juce::exactlyEqual (static_cast<double> (latency.getProperty (conduit::id::paramValue)),   150.0));
    REQUIRE (juce::exactlyEqual (static_cast<double> (latency.getProperty (conduit::id::paramMin)),     20.0));
    REQUIRE (juce::exactlyEqual (static_cast<double> (latency.getProperty (conduit::id::paramMax)),     500.0));
    REQUIRE (juce::exactlyEqual (static_cast<double> (latency.getProperty (conduit::id::paramDefault)), 150.0));

    // Echtzeit-Parameter-Ziel (Dual-State 6.1)
    REQUIRE (module.getParameterTarget (conduit::LinkAudioReceiveModule::latencyParamId) != nullptr);
    REQUIRE (module.getParameterTarget ("unbekannt") == nullptr);
}

//==============================================================================
TEST_CASE ("LinkAudioReceiveModule: Lifecycle über den GraphManager (Refcount, Bindung, Phase 1)",
           "[linkaudio][receive]")
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

    conduit::LinkClock clock (120.0, "ConduitTest");
    clock.prepare (48000.0);
    manager.setLinkClock (&clock);

    conduit::ClockBus bus;
    manager.setClockBus (&bus);

    REQUIRE_FALSE (clock.isAudioEnabled());

    //==========================================================================
    // Materialisierung: Quelle ohne Eingänge, Kontext hält den Audio-Refcount
    // (Discovery braucht aktives Link Audio)
    auto node = manager.addModuleNode (conduit::LinkAudioReceiveModule::staticModuleId, {});
    REQUIRE (node.isValid());
    manager.flushPendingTopologyUpdate();

    auto* module = dynamic_cast<conduit::LinkAudioReceiveModule*> (manager.getModuleFor (uuidOf (node)));
    REQUIRE (module != nullptr);
    REQUIRE (module->getTotalNumInputChannels()  == 0);
    REQUIRE (module->getTotalNumOutputChannels() == 2);
    REQUIRE (clock.isAudioEnabled());
    REQUIRE (module->getReceiveStatusForUi() == conduit::LinkAudioReceiveModule::ReceiveStatus::offline);
    REQUIRE_FALSE (module->hasActiveSource());

    //==========================================================================
    // Wunsch ohne Session-Match → searching (Property-Spiegelung Tree → Modul)
    node.setProperty (conduit::id::targetPeer,    "ConduitTest",   nullptr);
    node.setProperty (conduit::id::targetChannel, "gibts_nicht",   nullptr);
    REQUIRE (module->getTargetPeer() == "ConduitTest");
    REQUIRE (module->getTargetChannel() == "gibts_nicht");
    REQUIRE (module->getReceiveStatusForUi() == conduit::LinkAudioReceiveModule::ReceiveStatus::searching);
    REQUIRE_FALSE (module->hasActiveSource());

    //==========================================================================
    // Kanal-Discovery listet NUR Peer-Announcements — eigene Sinks tauchen
    // nicht auf (SDK Channels::sawAnnouncement, LinkClock-Header-Doku).
    // Die Bindung an einen echten Kanal verifiziert deshalb der Live-
    // Feldtest; hier: Wunsch zurücksetzen löst die Bindungssuche (offline).
    node.setProperty (conduit::id::targetPeer,    juce::String(), nullptr);
    node.setProperty (conduit::id::targetChannel, juce::String(), nullptr);
    REQUIRE (module->getReceiveStatusForUi() == conduit::LinkAudioReceiveModule::ReceiveStatus::offline);

    //==========================================================================
    // Delete Phase 1 (5.3): Wunsch aktiv → Modul geht offline, Refcount
    // wird frei (deferred Disable braucht den flush-Seam)
    node.setProperty (conduit::id::targetPeer,    "Ableton Live", nullptr);
    node.setProperty (conduit::id::targetChannel, "Track 1",      nullptr);
    REQUIRE (module->getReceiveStatusForUi() == conduit::LinkAudioReceiveModule::ReceiveStatus::searching);

    REQUIRE (manager.requestNodeDelete (uuidOf (node)));
    REQUIRE_FALSE (module->hasActiveSource());
    REQUIRE (module->getReceiveStatusForUi() == conduit::LinkAudioReceiveModule::ReceiveStatus::offline);

    clock.flushPendingAudioState();
    REQUIRE_FALSE (clock.isAudioEnabled());
}

//==============================================================================
TEST_CASE ("LinkAudioReceiveModule: findChannelKey matcht Peer + Name exakt", "[linkaudio][receive]")
{
    const std::vector<conduit::LinkClock::ChannelInfo> channels = {
        { 11, "Track 1", "Ableton Live" },
        { 22, "Track 1", "Anderer Peer" },
        { 33, "Track 2", "Ableton Live" },
    };

    using Module = conduit::LinkAudioReceiveModule;
    REQUIRE (Module::findChannelKey (channels, "Ableton Live", "Track 1") == 11);
    REQUIRE (Module::findChannelKey (channels, "Anderer Peer", "Track 1") == 22);
    REQUIRE (Module::findChannelKey (channels, "Ableton Live", "Track 2") == 33);
    REQUIRE (Module::findChannelKey (channels, "Ableton Live", "Track 3") == 0);   // Name fehlt
    REQUIRE (Module::findChannelKey (channels, "Dritter Peer", "Track 1") == 0);   // Peer fehlt
    REQUIRE (Module::findChannelKey ({}, "Ableton Live", "Track 1") == 0);         // leere Session
}

//==============================================================================
TEST_CASE ("LinkAudioReceiveModule: processBlock rendert beat-aligned aus dem Stream",
           "[linkaudio][receive]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LinkAudioReceiveModule module;

    constexpr int    blockSize     = 128;
    constexpr double sampleRate    = 48000.0;
    constexpr double beatsPerFrame = 120.0 / (60.0 * sampleRate);

    conduit::ClockBus bus;
    module.setClockBus (&bus);

    auto* latency = module.getParameterTarget (conduit::LinkAudioReceiveModule::latencyParamId);
    REQUIRE (latency != nullptr);
    latency->store (50.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    // Ohne ClockBus: Stille (Beat-Achse fehlt)
    module.setClockBus (nullptr);
    buffer.setSample (0, 0, 0.7f);
    module.processBlock (buffer, midi);
    REQUIRE (buffer.getMagnitude (0, 0, blockSize) < 1.0e-9f);
    module.setClockBus (&bus);

    // Slots über den Test-Seam injizieren (kein echtes Link nötig) und
    // synchron zum lokalen Beat rendern — nach dem Latenzfenster (50 ms)
    // liefert der Ausgang Signal.
    auto& stream = module.getStreamForTest();
    std::vector<std::int16_t> data (blockSize, 8000);

    double       senderBeat = 0.0;
    std::uint64_t count     = 1;
    bool         sawSignal  = false;

    for (int block = 0; block < 60; ++block)
    {
        REQUIRE (stream.pushBuffer (data.data(), blockSize, 1, sampleRate, 120.0,
                                    senderBeat, count));
        senderBeat += static_cast<double> (blockSize) * beatsPerFrame;
        ++count;

        bus.current.bpm              = 120.0;
        bus.current.sampleRate       = sampleRate;
        bus.current.beatAtBlockStart = static_cast<double> (block) * blockSize * beatsPerFrame;

        module.processBlock (buffer, midi);
        sawSignal = sawSignal || buffer.getMagnitude (0, 0, blockSize) > 0.1f;
    }

    REQUIRE (sawSignal);
    REQUIRE (stream.getStatusForUi() == conduit::LinkReceiveStream::Status::streaming);

    // Mono-Quelle liegt auf beiden Ausgängen
    REQUIRE (buffer.getMagnitude (1, 0, blockSize) > 0.1f);
}
