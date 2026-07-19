#include <algorithm>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/EngineProcessor.h"
#include "Core/GraphManager.h"
#include "Modules/LooperBigOutModule.h"
#include "TestSettingsFolder.h"

using conduit::LooperBigOutModule;
using Kind = LooperBigOutModule::Kind;
using Spec = LooperBigOutModule::OutputSpec;
using Structure = LooperBigOutModule::Structure;

//==============================================================================
TEST_CASE ("LooperBigOutModule: buildSpecs — Reihenfolge, Labels, Offsets", "[looper]")
{
    SECTION ("Minimal-Struktur (1 Looper / 1 Track): 7 Slots")
    {
        const auto specs = LooperBigOutModule::buildSpecs (Structure {});
        REQUIRE (specs.size() == 7);
        REQUIRE (specs[0] == Spec { Kind::track, 1, 1, 0 });
        REQUIRE (specs[1] == Spec { Kind::bus, 1, 0, 0 });
        REQUIRE (specs[2] == Spec { Kind::send, 0, 0, 1 });
        REQUIRE (specs[5] == Spec { Kind::send, 0, 0, 4 });
        REQUIRE (specs[6] == Spec { Kind::master, 0, 0, 0 });

        REQUIRE (LooperBigOutModule::channelOffsetOf (specs, specs[6]) == 12);
        REQUIRE (LooperBigOutModule::channelOffsetOf (specs, { Kind::track, 2, 1, 0 }) == -1);
    }

    SECTION ("Vollausbau-Anteile: Tracks geflattet Looper-major, dann Busse")
    {
        Structure structure;
        structure.numLoopers = 2;
        structure.numTracks = { 2, 1, 4, 4 };   // Looper 3/4 inaktiv

        const auto specs = LooperBigOutModule::buildSpecs (structure);
        REQUIRE (specs.size() == 2 + 1 + 2 + 4 + 1);   // Tracks + Busse + Sends + Master
        REQUIRE (specs[0] == Spec { Kind::track, 1, 1, 0 });
        REQUIRE (specs[1] == Spec { Kind::track, 1, 2, 0 });
        REQUIRE (specs[2] == Spec { Kind::track, 2, 1, 0 });
        REQUIRE (specs[3] == Spec { Kind::bus, 1, 0, 0 });
        REQUIRE (specs[4] == Spec { Kind::bus, 2, 0, 0 });
        REQUIRE (specs[5].kind == Kind::send);

        // Volle 4×4-Struktur: 16 Track-Outs + 4 Busse + 4 Sends + Master
        Structure full;
        full.numLoopers = 4;
        full.numTracks = { 4, 4, 4, 4 };
        REQUIRE (LooperBigOutModule::buildSpecs (full).size() == 25);
    }

    SECTION ("Labels")
    {
        REQUIRE (LooperBigOutModule::outputLabel ({ Kind::track, 2, 3, 0 })
                 == juce::String::fromUTF8 ("Looper 2 · Track 3"));
        REQUIRE (LooperBigOutModule::outputLabel ({ Kind::bus, 1, 0, 0 })
                 == juce::String::fromUTF8 ("Looper 1 · Bus"));
        REQUIRE (LooperBigOutModule::outputLabel ({ Kind::send, 0, 0, 2 }) == "Send 2");
        REQUIRE (LooperBigOutModule::outputLabel ({ Kind::master, 0, 0, 0 }) == "Master");
    }

    SECTION ("Clamps: ungültige Struktur fällt auf 1/1 zurück")
    {
        Structure broken;
        broken.numLoopers = 0;
        broken.numTracks = { -3, 9, 1, 1 };
        const auto specs = LooperBigOutModule::buildSpecs (broken);
        REQUIRE (specs[0] == Spec { Kind::track, 1, 1, 0 });
        REQUIRE (specs.size() == 7);
    }
}

TEST_CASE ("LooperBigOutModule: <Outputs>-Schema-Roundtrip", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    Structure structure;
    structure.numLoopers = 2;
    structure.numTracks = { 2, 1, 1, 1 };
    const auto specs = LooperBigOutModule::buildSpecs (structure);

    juce::ValueTree node (conduit::id::node);
    LooperBigOutModule::applyOutputConfig (node, specs);

    REQUIRE ((int) node.getProperty (conduit::id::numInputChannels) == 0);
    REQUIRE ((int) node.getProperty (conduit::id::numOutputChannels)
             == (int) specs.size() * LooperBigOutModule::slotWidth);
    REQUIRE (LooperBigOutModule::readOutputConfig (node) == specs);

    // Leere Liste → Master-Fallback
    LooperBigOutModule::applyOutputConfig (node, {});
    const auto fallback = LooperBigOutModule::readOutputConfig (node);
    REQUIRE (fallback.size() == 1);
    REQUIRE (fallback[0].kind == Kind::master);
}

//==============================================================================
TEST_CASE ("Big Looper Out: Auto-Follow der Struktur + Kabel-Remap", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };
    auto& manager = engine.getGraphManager();

    engine.setPlayConfigDetails (2, 2, 48000.0, 480);
    engine.prepareToPlay (48000.0, 480);

    juce::AudioBuffer<float> ioBuffer { 2, 480 };
    juce::MidiBuffer midi;
    const auto settleSwap = [&]
    {
        manager.flushPendingTopologyUpdate();
        for (int i = 0; i < 100 && manager.isWaitingForSilence(); ++i)
        {
            ioBuffer.clear();
            engine.processBlock (ioBuffer, midi);
            manager.flushPendingTopologyUpdate();
        }
        REQUIRE_FALSE (manager.isWaitingForSilence());
    };

    // Frisch aus dem Browser: folgt der Werks-Struktur (1 Looper / 1 Track)
    auto node = manager.addModuleNode (LooperBigOutModule::staticModuleId, {});
    REQUIRE (node.isValid());
    settleSwap();
    REQUIRE (node.getProperty (conduit::id::nodeError).toString().isEmpty());

    REQUIRE (node.getChildWithName (conduit::id::outputs).getNumChildren() == 7);
    REQUIRE ((int) node.getProperty (conduit::id::numOutputChannels) == 14);

    const auto bigOutUuid = node.getProperty (conduit::id::nodeId).toString();
    const auto audioOutUuid = engine.getRootState()
        .getChildWithName (conduit::id::nodes)
        .getChildWithProperty (conduit::id::factoryId, juce::String ("audio_output"))
        .getProperty (conduit::id::nodeId).toString();
    REQUIRE (audioOutUuid.isNotEmpty());

    // Master-Slot (Offset 12) an den Hardware-Out verkabeln
    REQUIRE (manager.addConnection (bigOutUuid, 12, audioOutUuid, 0));
    REQUIRE (manager.addConnection (bigOutUuid, 13, audioOutUuid, 1));
    settleSwap();

    const auto masterSourceChannels = [&]
    {
        std::vector<int> channels;
        const auto connections = engine.getRootState().getChildWithName (conduit::id::connections);
        for (int i = 0; i < connections.getNumChildren(); ++i)
            if (const auto connection = connections.getChild (i);
                connection.getProperty (conduit::id::sourceNodeId).toString() == bigOutUuid)
                channels.push_back ((int) connection.getProperty (conduit::id::sourceChannel));
        std::sort (channels.begin(), channels.end());
        return channels;
    };

    SECTION ("Track dazu: Slots wachsen, Master-Kabel rückt nach hinten")
    {
        Structure structure;
        structure.numTracks = { 2, 1, 1, 1 };
        manager.setLooperStructure (structure);
        settleSwap();

        REQUIRE (node.getChildWithName (conduit::id::outputs).getNumChildren() == 8);
        REQUIRE ((int) node.getProperty (conduit::id::numOutputChannels) == 16);
        REQUIRE (masterSourceChannels() == std::vector<int> { 14, 15 });

        // Kabel auf den neuen Track-Out (Looper 1 · Track 2, Offset 2)
        REQUIRE (manager.addConnection (bigOutUuid, 2, audioOutUuid, 0));

        // Track wieder weg: Track-Kabel stirbt, Master-Kabel rückt zurück
        manager.setLooperStructure (Structure {});
        settleSwap();

        REQUIRE (node.getChildWithName (conduit::id::outputs).getNumChildren() == 7);
        REQUIRE (masterSourceChannels() == std::vector<int> { 12, 13 });
    }

    SECTION ("Zweiter Looper: Offsets aller späteren Sektionen verschieben sich")
    {
        Structure structure;
        structure.numLoopers = 2;
        structure.numTracks = { 1, 1, 1, 1 };
        manager.setLooperStructure (structure);
        settleSwap();

        // track(1,1), track(2,1), bus(1), bus(2), sends ×4, master = 9 Slots
        REQUIRE (node.getChildWithName (conduit::id::outputs).getNumChildren() == 9);
        REQUIRE (masterSourceChannels() == std::vector<int> { 16, 17 });
    }

    SECTION ("Unveränderte Struktur: No-op (keine Re-Materialisierung nötig)")
    {
        const auto outputsBefore = node.getChildWithName (conduit::id::outputs);
        manager.setLooperStructure (Structure {});
        REQUIRE (node.getChildWithName (conduit::id::outputs) == outputsBefore);
    }

    SECTION ("Stale-Kabel auf Out-of-Range-Kanal überlebt den Swap nicht den Graph-Aufbau")
    {
        // Simuliert einen Undo-Rest: Connection jenseits der Kanalzahl —
        // der Topologie-Aufbau muss das aushalten (kein Crash, kein Hänger)
        juce::ValueTree stale (conduit::id::connection);
        stale.setProperty (conduit::id::sourceNodeId, bigOutUuid, nullptr);
        stale.setProperty (conduit::id::sourceChannel, 40, nullptr);
        stale.setProperty (conduit::id::destNodeId, audioOutUuid, nullptr);
        stale.setProperty (conduit::id::destChannel, 1, nullptr);
        engine.getRootState().getChildWithName (conduit::id::connections)
            .appendChild (stale, nullptr);

        settleSwap();
        for (int block = 0; block < 4; ++block)
        {
            ioBuffer.clear();
            engine.processBlock (ioBuffer, midi);
        }
        SUCCEED ("Out-of-Range-Kabel toleriert");
    }
}
