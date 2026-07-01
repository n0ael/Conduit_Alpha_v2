#include <atomic>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/LinkClock.h"
#include "Core/NodeUiRegistry.h"
#include "Modules/LinkAudioSendModule.h"
#include "Modules/ModuleFactory.h"

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

juce::String uuidOf (const juce::ValueTree& nodeTree)
{
    return nodeTree.getProperty (conduit::id::nodeId).toString();
}

} // namespace

//==============================================================================
TEST_CASE ("TPDF-Dither: Mittelwert ~0, Fehler in ±1.5 LSB, deterministisch pro Seed", "[linkaudio]")
{
    constexpr int numSamples = 200000;
    constexpr float input = 0.25f;                       // 8191.75 — liegt zwischen zwei LSB-Stufen
    constexpr double ideal = 0.25 * 32767.0;

    std::vector<float> signal (numSamples, input);
    const float* channels[] = { signal.data() };
    std::vector<std::int16_t> quantized (numSamples, 0);

    std::uint32_t seed = 0x6c078965u;
    conduit::LinkAudioSendModule::convertToInt16Tpdf (channels, 1, numSamples,
                                                      quantized.data(), seed);

    double errorSum = 0.0;
    double maxAbsError = 0.0;
    bool sawBothNeighbours = false;
    int count8191 = 0, count8192 = 0;

    for (const auto value : quantized)
    {
        const auto error = static_cast<double> (value) - ideal;
        errorSum += error;
        maxAbsError = juce::jmax (maxAbsError, std::abs (error));
        count8191 += value == 8191 ? 1 : 0;
        count8192 += value == 8192 ? 1 : 0;
    }

    sawBothNeighbours = count8191 > 0 && count8192 > 0;

    // TPDF: unbiased (Mittelwert des Fehlers → 0), Spitze ±1 LSB über der
    // Rundung (Fehlerfenster [-1.5, +1.5] LSB), und der Wert zwischen den
    // Stufen MUSS auf beide Nachbarstufen verteilt werden (kein nacktes
    // Truncate/Round — das war der Sinn des Dithers)
    REQUIRE (std::abs (errorSum / numSamples) < 0.05);
    REQUIRE (maxAbsError <= 1.5);
    REQUIRE (sawBothNeighbours);

    // Deterministisch pro Seed (3.1) — identischer Lauf, identisches Ergebnis
    std::vector<std::int16_t> rerun (numSamples, 0);
    std::uint32_t seed2 = 0x6c078965u;
    conduit::LinkAudioSendModule::convertToInt16Tpdf (channels, 1, numSamples,
                                                      rerun.data(), seed2);
    REQUIRE (rerun == quantized);
    REQUIRE (seed2 == seed);
}

//==============================================================================
TEST_CASE ("TPDF-Dither: Stereo interleaved, exakt numFrames × numChannels Samples", "[linkaudio]")
{
    constexpr int numFrames = 64;

    std::vector<float> left  (numFrames,  0.5f);
    std::vector<float> right (numFrames, -0.5f);
    const float* channels[] = { left.data(), right.data() };

    // Sentinels hinter dem Soll-Ende: der Konverter darf exakt
    // numFrames × 2 Samples schreiben — der v1-Frames/Samples-Grenzfall
    constexpr std::int16_t sentinel = 0x7abc;
    std::vector<std::int16_t> dest (numFrames * 2 + 4, sentinel);

    std::uint32_t seed = 1u;
    conduit::LinkAudioSendModule::convertToInt16Tpdf (channels, 2, numFrames,
                                                      dest.data(), seed);

    for (int frame = 0; frame < numFrames; ++frame)
    {
        REQUIRE (std::abs (dest[(size_t) frame * 2]     - 16383.5) <= 1.5);  // L interleaved auf gerade Indizes
        REQUIRE (std::abs (dest[(size_t) frame * 2 + 1] + 16383.5) <= 1.5);  // R auf ungerade
    }

    for (size_t i = (size_t) numFrames * 2; i < dest.size(); ++i)
        REQUIRE (dest[i] == sentinel);
}

//==============================================================================
TEST_CASE ("LinkClock::Sink: Kapazität in SAMPLES (Frames × Kanäle), wächst nur", "[linkaudio]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LinkClock clock (120.0, "ConduitTest");
    clock.prepare (48000.0);

    // 32-Sample-Block, Stereo → 64 SAMPLES (7.2) — wer hier Frames anlegt,
    // fällt durch
    auto sink = clock.createSink ("samples_not_frames", 32 * 2);
    REQUIRE (sink->getMaxNumSamples() == 64);

    sink->requestMaxNumSamples (128);
    REQUIRE (sink->getMaxNumSamples() == 128);

    sink->requestMaxNumSamples (64);                  // schrumpfen ist ein Link-No-op
    REQUIRE (sink->getMaxNumSamples() == 128);

    // Commit ohne Subscriber: noBuffer — und niemals ein Crash. rejected
    // bleibt Programmierfehlern vorbehalten (kein captureClockState).
    std::vector<std::int16_t> samples (64, 0);

    auto stale = sink->commitFromClockState (samples.data(), 32, 2, {});
    REQUIRE (stale == conduit::LinkClock::Sink::CommitResult::rejected);  // kein Block-Stash

    const auto state = clock.captureClockState (32);
    auto result = sink->commitFromClockState (samples.data(), 32, 2, state);
    REQUIRE (result == conduit::LinkClock::Sink::CommitResult::noBuffer);
}

//==============================================================================
TEST_CASE ("LinkClock Receive: ChannelKey-Round-Trip + Discovery ohne Peers (7.2)", "[linkaudio]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LinkClock clock (120.0, "ConduitTest");
    clock.prepare (48000.0);

    // Frisch konstruiert, kein Peer im Test-Kontext angemeldet: keine eigenen
    // Sinks announcet → Kanalliste leer (Peers im LAN sind test-extern, daher
    // NICHT auf Leerheit prüfen, sondern nur auf konsistente Struktur).
    for (const auto& ch : clock.availableChannels())
        REQUIRE (ch.id != 0);  // eine announcete ID ist nie 0

    // createSource packt/entpackt den opaken ChannelKey verlustfrei durch die
    // 8-Byte-NodeId-Kodierung (Big-Endian) — inkl. der Grenzwerte.
    for (const conduit::LinkClock::ChannelKey key :
         { conduit::LinkClock::ChannelKey { 0x0123456789abcdefULL },
           conduit::LinkClock::ChannelKey { 0xffffffffffffffffULL },
           conduit::LinkClock::ChannelKey { 1 } })
    {
        auto source = clock.createSource (key, [] (const conduit::LinkClock::Source::ReceivedBuffer&) {});
        REQUIRE (source != nullptr);
        REQUIRE (source->channelId() == key);
    }

    // key == 0 (unbekannt): Source entsteht trotzdem, empfängt nur nichts.
    auto idle = clock.createSource (0, [] (const conduit::LinkClock::Source::ReceivedBuffer&) {});
    REQUIRE (idle != nullptr);
    REQUIRE (idle->channelId() == 0);
}

//==============================================================================
TEST_CASE ("LinkAudioSendModule: Sink-Lifecycle über den GraphManager (7.2)", "[linkaudio]")
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
    // Materialisierung: Default = 1 Stereo-Eingang, Kanal-Name {moduleId}/input1
    const auto node1 = manager.addModuleNode (conduit::LinkAudioSendModule::staticModuleId, {});
    REQUIRE (node1.isValid());
    manager.flushPendingTopologyUpdate();

    const auto module1Id = node1.getProperty (conduit::id::moduleId).toString();
    auto* module1 = dynamic_cast<conduit::LinkAudioSendModule*> (manager.getModuleFor (uuidOf (node1)));
    REQUIRE (module1 != nullptr);
    REQUIRE (module1->getNumSlots() == 1);
    REQUIRE (module1->getTotalNumInputChannels()  == 2);   // Stereo-Eingang
    REQUIRE (module1->getTotalNumOutputChannels() == 0);   // reiner Sender
    REQUIRE (module1->getSinkNames() == juce::StringArray (module1Id + "/input1"));
    REQUIRE (clock.isAudioEnabled());

    // Zweites Modul mit gemischter Anlege-Config (2 mono + 1 stereo → 4 Kanäle)
    const auto node2 = manager.addModuleNode (
        conduit::LinkAudioSendModule::staticModuleId, {},
        [] (juce::ValueTree& tree)
        {
            using Mode = conduit::LinkAudioSendModule::InputMode;
            conduit::LinkAudioSendModule::applyInputConfig (tree, { Mode::mono, Mode::mono, Mode::stereo });
        });
    manager.flushPendingTopologyUpdate();

    auto* module2 = dynamic_cast<conduit::LinkAudioSendModule*> (manager.getModuleFor (uuidOf (node2)));
    REQUIRE (module2 != nullptr);
    REQUIRE (module2->getNumSlots() == 3);
    REQUIRE (module2->getTotalNumInputChannels() == 4);
    REQUIRE (clock.isAudioEnabled());

    //==========================================================================
    // Rename propagiert live auf ALLE Sinks (Präfix-Wechsel, sanitiert, undo-fähig)
    REQUIRE (manager.renameNode (uuidOf (node1), "Drum Bus"));
    REQUIRE (node1.getProperty (conduit::id::moduleId).toString() == "drum_bus");
    REQUIRE (module1->getSinkNames() == juce::StringArray ("drum_bus/input1"));

    undoManager.undo();
    REQUIRE (module1->getSinkNames() == juce::StringArray (module1Id + "/input1"));

    //==========================================================================
    // Ohne Subscriber: Status announced (Sinks senden erst bei Source, 7.2)
    bus.current = clock.captureClockState (32);
    juce::AudioBuffer<float> buffer (2, 32);
    buffer.clear();
    juce::MidiBuffer midi;
    module1->processBlock (buffer, midi);
    REQUIRE (module1->getSendStatusForUi() == conduit::LinkAudioSendModule::SendStatus::announced);

    //==========================================================================
    // Delete Phase 1 (5.3): Sinks sofort weg, Refcount −1 — Audio bleibt an
    // (Modul 2 lebt), die Sink-Destruktion folgt nach dem Epoch-Handshake
    REQUIRE (manager.requestNodeDelete (uuidOf (node1)));
    REQUIRE (module1->getSinkNames().isEmpty());
    REQUIRE (module1->isSinkRetirePending());
    REQUIRE (module1->getSendStatusForUi() == conduit::LinkAudioSendModule::SendStatus::offline);
    REQUIRE (clock.isAudioEnabled());

    // Audio-Thread-Surrogat: ein Block nach dem Store → Handshake erfüllt
    module1->processBlock (buffer, midi);
    module1->flushPendingSinkRetirement();
    REQUIRE_FALSE (module1->isSinkRetirePending());

    // Phase 2: Subtree weg, Modul destruiert — Refcount bleibt balanciert
    manager.flushPendingTopologyUpdate();
    REQUIRE (manager.getModuleFor (uuidOf (node1)) == nullptr);
    REQUIRE (clock.isAudioEnabled());

    // Letztes Send-Modul weg → Audio deaktiviert (Refcount 0)
    REQUIRE (manager.requestNodeDelete (uuidOf (node2)));
    manager.flushPendingTopologyUpdate();
    REQUIRE_FALSE (clock.isAudioEnabled());
}

//==============================================================================
TEST_CASE ("LinkAudioSendModule: Destruktion ohne Phase 1 balanciert den Refcount", "[linkaudio]")
{
    // Preset-Load/Shutdown entfernen Nodes ohne requestNodeDelete — der
    // Destruktor muss enableAudio(false) selbst nachziehen
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LinkClock clock (120.0, "ConduitTest");
    clock.prepare (48000.0);

    const std::vector<conduit::SendInputConfig> oneStereo {
        { "in-uuid", 2, "input1", "in1_gain" } };

    {
        conduit::LinkAudioSendModule module;
        module.setLinkAudioContext (&clock, "solo");
        module.applySendConfig (oneStereo);
        REQUIRE (module.prepareForGraph (48000.0, 32).wasOk());
        REQUIRE (module.getSinkNames() == juce::StringArray ("solo/input1"));
        REQUIRE (clock.isAudioEnabled());

        // Re-Prepare mit größerem Block ist idempotent (kein zweiter Sink,
        // kein doppelter Refcount), Kapazität wächst mit
        REQUIRE (module.prepareForGraph (48000.0, 64).wasOk());
        REQUIRE (module.getSinkNames() == juce::StringArray ("solo/input1"));
    }

    REQUIRE_FALSE (clock.isAudioEnabled());

    // Ohne Link-Kontext (Tests): kein Sink, Status offline, Input unangetastet
    conduit::LinkAudioSendModule offline;
    offline.applySendConfig (oneStereo);
    REQUIRE (offline.prepareForGraph (48000.0, 32).wasOk());

    juce::AudioBuffer<float> buffer (2, 32);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 32; ++i)
            buffer.setSample (channel, i, 0.25f);

    juce::MidiBuffer midi;
    offline.processBlock (buffer, midi);

    REQUIRE (offline.getSendStatusForUi() == conduit::LinkAudioSendModule::SendStatus::offline);
    REQUIRE (buffer.getSample (0, 17) == Approx (0.25f));  // reiner Sender liest nur
    REQUIRE (buffer.getSample (1, 31) == Approx (0.25f));
}

//==============================================================================
TEST_CASE ("LinkAudioSendModule: Retire-Handshake unter echtem Audio-Thread", "[linkaudio][threading]")
{
    // TSan-Ziel (13.4): Phase 1 auf dem Message Thread, processBlock pumpt
    // parallel — der Sink darf erst destruieren, wenn kein Block mehr den
    // alten Pointer halten kann (Epoch-Handshake, Modul-Doku)
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LinkClock clock (120.0, "ConduitTest");
    clock.prepare (48000.0);

    conduit::ClockBus bus;

    conduit::LinkAudioSendModule module;
    module.setLinkAudioContext (&clock, "stress");
    module.applySendConfig ({ { "a", 2, "input1", "in1_gain" },
                             { "b", 1, "input2", "in2_gain" } });
    module.setClockBus (&bus);
    REQUIRE (module.prepareForGraph (48000.0, 32).wasOk());

    std::atomic<bool> keepRunning { true };
    std::atomic<int> blocksPumped { 0 };

    std::thread audioThread ([&]
    {
        juce::AudioBuffer<float> buffer (2, 32);
        juce::MidiBuffer midi;

        while (keepRunning.load())
        {
            bus.current = clock.captureClockState (32);  // Stash + Commit: gleicher Thread
            module.processBlock (buffer, midi);
            blocksPumped.fetch_add (1);
            std::this_thread::yield();
        }
    });

    // Den Audio-Thread sicher ein paar Blöcke arbeiten lassen
    while (blocksPumped.load() < 8)
        std::this_thread::yield();

    module.releaseSessionResources();  // Phase 1
    REQUIRE (module.isSinkRetirePending());

    while (module.isSinkRetirePending())
    {
        module.flushPendingSinkRetirement();
        std::this_thread::yield();
    }

    keepRunning.store (false);
    audioThread.join();

    REQUIRE_FALSE (clock.isAudioEnabled());
    REQUIRE (module.getSendStatusForUi() == conduit::LinkAudioSendModule::SendStatus::offline);
}

//==============================================================================
TEST_CASE ("LinkAudioSendModule: Migration Alt-Stereo-Send → Multi-Input (v1→v2)", "[linkaudio]")
{
    using Send = conduit::LinkAudioSendModule;

    // Alt-Node: fester Stereo-Send ohne <Inputs>, moduleId trägt den Namen
    juce::ValueTree node (conduit::id::node);
    node.setProperty (conduit::id::nodeId,           juce::Uuid().toString(), nullptr);
    node.setProperty (conduit::id::factoryId,        Send::staticModuleId,    nullptr);
    node.setProperty (conduit::id::moduleId,         "old_send",              nullptr);
    node.setProperty (conduit::id::stateVersion,     1,                       nullptr);
    node.setProperty (conduit::id::numInputChannels,  2,                      nullptr);
    node.setProperty (conduit::id::numOutputChannels, 2,                      nullptr);
    node.appendChild (juce::ValueTree (conduit::id::parameters), nullptr);

    Send::migrate (node);

    const auto inputs = node.getChildWithName (conduit::id::inputs);
    REQUIRE (inputs.isValid());
    REQUIRE (inputs.getNumChildren() == 1);
    REQUIRE (inputs.getChild (0).getProperty (conduit::id::inputMode).toString() == Send::modeStereo);
    // Namensstabilität: alter moduleId wird zum Auto-Namen des Eingangs
    REQUIRE (inputs.getChild (0).getProperty (conduit::id::inputAutoName).toString() == "old_send");
    REQUIRE ((int) node.getProperty (conduit::id::numInputChannels)  == 2);
    REQUIRE ((int) node.getProperty (conduit::id::numOutputChannels) == 0);   // reiner Sender
    REQUIRE ((int) node.getProperty (conduit::id::stateVersion) == Send::stateVersion);
    REQUIRE (node.getChildWithName (conduit::id::parameters)
                 .getChildWithProperty (conduit::id::paramId, "in1_gain").isValid());

    // Idempotent
    Send::migrate (node);
    REQUIRE (node.getChildWithName (conduit::id::inputs).getNumChildren() == 1);
}

//==============================================================================
TEST_CASE ("LinkAudioSendModule: applyInputConfig + readInputConfig (Offsets/Namen)", "[linkaudio]")
{
    using Send = conduit::LinkAudioSendModule;
    using Mode = Send::InputMode;

    juce::ValueTree node (conduit::id::node);
    node.appendChild (juce::ValueTree (conduit::id::parameters), nullptr);

    Send::applyInputConfig (node, { Mode::mono, Mode::stereo });

    REQUIRE ((int) node.getProperty (conduit::id::numInputChannels)  == 3);   // 1 + 2
    REQUIRE ((int) node.getProperty (conduit::id::numOutputChannels) == 0);

    auto cfg = Send::readInputConfig (node);
    REQUIRE (cfg.size() == 2);
    REQUIRE (cfg[0].width == 1);
    REQUIRE (cfg[0].effectiveName == "input1");
    REQUIRE (cfg[0].gainParamId == "in1_gain");
    REQUIRE (cfg[1].width == 2);
    REQUIRE (cfg[1].effectiveName == "input2");
    REQUIRE (cfg[1].gainParamId == "in2_gain");

    // userName überschreibt, autoName ist Fallback vor "input{n}"
    auto inputs = node.getChildWithName (conduit::id::inputs);
    inputs.getChild (0).setProperty (conduit::id::inputUserName, "kick",  nullptr);
    inputs.getChild (1).setProperty (conduit::id::inputAutoName, "synth", nullptr);
    cfg = Send::readInputConfig (node);
    REQUIRE (cfg[0].effectiveName == "kick");
    REQUIRE (cfg[1].effectiveName == "synth");

    // Re-Apply ersetzt <Inputs> + Gain-Params ohne Dubletten
    Send::applyInputConfig (node, { Mode::mono });
    REQUIRE (node.getChildWithName (conduit::id::inputs).getNumChildren() == 1);
    int gainParams = 0;
    const auto params = node.getChildWithName (conduit::id::parameters);
    for (int i = 0; i < params.getNumChildren(); ++i)
        if (params.getChild (i).getProperty (conduit::id::paramId).toString().endsWith ("_gain"))
            ++gainParams;
    REQUIRE (gainParams == 1);
}

//==============================================================================
TEST_CASE ("LinkAudioSendModule: getParameterTarget mappt in{n}_gain auf getrennte Slots", "[linkaudio]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LinkAudioSendModule module;
    module.applySendConfig ({ { "a", 1, "input1", "in1_gain" },
                             { "b", 2, "input2", "in2_gain" } });
    REQUIRE (module.prepareForGraph (48000.0, 32).wasOk());

    REQUIRE (module.getNumSlots() == 2);
    auto* g1 = module.getParameterTarget ("in1_gain");
    auto* g2 = module.getParameterTarget ("in2_gain");
    REQUIRE (g1 != nullptr);
    REQUIRE (g2 != nullptr);
    REQUIRE (g1 != g2);
    REQUIRE (module.getParameterTarget ("unknown") == nullptr);
}
