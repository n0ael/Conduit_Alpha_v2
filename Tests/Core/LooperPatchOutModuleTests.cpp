#include <algorithm>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/EngineProcessor.h"
#include "Core/GraphManager.h"
#include "Modules/LooperPatchOutModule.h"
#include "TestSettingsFolder.h"
#include "UI/LooperPatchOutPanel.h"

using conduit::LooperPatchOutModule;
using Kind = LooperPatchOutModule::Kind;
using Spec = LooperPatchOutModule::OutputSpec;
using Structure = LooperPatchOutModule::Structure;

//==============================================================================
TEST_CASE ("LooperPatchOutModule: buildSpecs — Reihenfolge, Labels, Offsets", "[looper]")
{
    SECTION ("Minimal-Struktur (1 Looper / 1 Track): 7 Slots")
    {
        const auto specs = LooperPatchOutModule::buildSpecs (Structure {});
        REQUIRE (specs.size() == 7);
        REQUIRE (specs[0] == Spec { Kind::track, 1, 1, 0 });
        REQUIRE (specs[1] == Spec { Kind::bus, 1, 0, 0 });
        REQUIRE (specs[2] == Spec { Kind::send, 0, 0, 1 });
        REQUIRE (specs[5] == Spec { Kind::send, 0, 0, 4 });
        REQUIRE (specs[6] == Spec { Kind::master, 0, 0, 0 });

        REQUIRE (LooperPatchOutModule::channelOffsetOf (specs, specs[6]) == 12);
        REQUIRE (LooperPatchOutModule::channelOffsetOf (specs, { Kind::track, 2, 1, 0 }) == -1);
    }

    SECTION ("Vollausbau-Anteile: Tracks geflattet Looper-major, dann Busse")
    {
        Structure structure;
        structure.numLoopers = 2;
        structure.numTracks = { 2, 1, 4, 4 };   // Looper 3/4 inaktiv

        const auto specs = LooperPatchOutModule::buildSpecs (structure);
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
        REQUIRE (LooperPatchOutModule::buildSpecs (full).size() == 25);
    }

    SECTION ("Labels — Tracks global im 4er-Raster nummeriert (Skizze 19.07.2026)")
    {
        REQUIRE (LooperPatchOutModule::outputLabel ({ Kind::track, 1, 1, 0 })
                 == juce::String::fromUTF8 ("Looper 1 · Track 1"));
        REQUIRE (LooperPatchOutModule::outputLabel ({ Kind::track, 2, 1, 0 })
                 == juce::String::fromUTF8 ("Looper 2 · Track 5"));
        REQUIRE (LooperPatchOutModule::outputLabel ({ Kind::track, 2, 3, 0 })
                 == juce::String::fromUTF8 ("Looper 2 · Track 7"));
        REQUIRE (LooperPatchOutModule::outputLabel ({ Kind::track, 4, 1, 0 })
                 == juce::String::fromUTF8 ("Looper 4 · Track 13"));
        REQUIRE (LooperPatchOutModule::outputLabel ({ Kind::bus, 1, 0, 0 })
                 == juce::String::fromUTF8 ("Looper 1 · Bus"));
        REQUIRE (LooperPatchOutModule::outputLabel ({ Kind::send, 0, 0, 2 }) == "Send 2");
        REQUIRE (LooperPatchOutModule::outputLabel ({ Kind::master, 0, 0, 0 }) == "Master");
    }

    SECTION ("meterChannelOf: stabile 4er-Raster-Kanäle, unabhängig von der Struktur")
    {
        REQUIRE (LooperPatchOutModule::meterChannelOf ({ Kind::track, 1, 1, 0 }) == 0);
        REQUIRE (LooperPatchOutModule::meterChannelOf ({ Kind::track, 1, 2, 0 }) == 2);
        REQUIRE (LooperPatchOutModule::meterChannelOf ({ Kind::track, 2, 1, 0 }) == 8);
        REQUIRE (LooperPatchOutModule::meterChannelOf ({ Kind::track, 4, 4, 0 }) == 30);
        REQUIRE (LooperPatchOutModule::meterChannelOf ({ Kind::bus, 1, 0, 0 }) == 32);
        REQUIRE (LooperPatchOutModule::meterChannelOf ({ Kind::bus, 4, 0, 0 }) == 38);
        REQUIRE (LooperPatchOutModule::meterChannelOf ({ Kind::send, 0, 0, 1 }) == 40);
        REQUIRE (LooperPatchOutModule::meterChannelOf ({ Kind::send, 0, 0, 4 }) == 46);
        REQUIRE (LooperPatchOutModule::meterChannelOf ({ Kind::master, 0, 0, 0 }) == 48);
        REQUIRE (LooperPatchOutModule::meterChannelCount == 50);

        // Ungültige Specs fallen sauber raus
        REQUIRE (LooperPatchOutModule::meterChannelOf ({ Kind::track, 5, 1, 0 }) == -1);
        REQUIRE (LooperPatchOutModule::meterChannelOf ({ Kind::send, 0, 0, 5 }) == -1);
    }

    SECTION ("Clamps: ungültige Struktur fällt auf 1/1 zurück")
    {
        Structure broken;
        broken.numLoopers = 0;
        broken.numTracks = { -3, 9, 1, 1 };
        const auto specs = LooperPatchOutModule::buildSpecs (broken);
        REQUIRE (specs[0] == Spec { Kind::track, 1, 1, 0 });
        REQUIRE (specs.size() == 7);
    }
}

TEST_CASE ("LooperPatchOutModule: <Outputs>-Schema-Roundtrip", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    Structure structure;
    structure.numLoopers = 2;
    structure.numTracks = { 2, 1, 1, 1 };
    const auto specs = LooperPatchOutModule::buildSpecs (structure);

    juce::ValueTree node (conduit::id::node);
    LooperPatchOutModule::applyOutputConfig (node, specs);

    REQUIRE ((int) node.getProperty (conduit::id::numInputChannels) == 0);
    REQUIRE ((int) node.getProperty (conduit::id::numOutputChannels)
             == (int) specs.size() * LooperPatchOutModule::slotWidth);
    REQUIRE (LooperPatchOutModule::readOutputConfig (node) == specs);

    // Leere Liste → Master-Fallback
    LooperPatchOutModule::applyOutputConfig (node, {});
    const auto fallback = LooperPatchOutModule::readOutputConfig (node);
    REQUIRE (fallback.size() == 1);
    REQUIRE (fallback[0].kind == Kind::master);
}

TEST_CASE ("LooperPatchOutPanel: Zeilenmodell — Sektions-Überschriften, Collapse, Kabel-Sammelpunkt",
           "[looper]")
{
    Structure structure;
    structure.numLoopers = 2;
    structure.numTracks = { 2, 2, 1, 1 };
    const auto specs = LooperPatchOutModule::buildSpecs (structure);
    // specs: 4 Tracks + 2 Busse + 4 Sends + Master = 11 Slots

    using Panel = conduit::LooperPatchOutPanel;

    SECTION ("Alles ausgeklappt: Überschriften Looper 1/2, Busse, Sends")
    {
        const auto rows = Panel::buildRows (specs, 0);

        REQUIRE (rows.size() == specs.size() + 4);   // 2× Looper + Busse + Sends
        REQUIRE (rows[0].isHeader());
        REQUIRE (rows[0].label == "Looper 1");
        REQUIRE_FALSE (rows[0].collapsed);
        REQUIRE (rows[1].label == "Track 1");
        REQUIRE (rows[1].slotIndex == 0);
        REQUIRE (rows[3].label == "Looper 2");
        REQUIRE (rows[4].label == "Track 5");        // global im 4er-Raster
        REQUIRE (rows[4].slotIndex == 2);
        REQUIRE (rows[6].isHeader());
        REQUIRE (rows[6].label == "Busse");
        REQUIRE (rows[7].label == "Bus 1");          // statt „Looper 1 · Bus"
        REQUIRE (rows[7].slotIndex == 4);
        REQUIRE (rows[9].label == "Returns");   // Überschrift (User 19.07.2026)
        REQUIRE (rows[10].label == "Send 1");
        REQUIRE (rows.back().label == "Master");

        // Geometrie: Überschriften verschieben die Slot-Mitten
        REQUIRE (Panel::rowCentreYForSlot (specs, 0, 0) == 6 + 1 * 30 + 15);
        REQUIRE (Panel::rowCentreYForSlot (specs, 0, 2)
                 - Panel::rowCentreYForSlot (specs, 0, 1) == 2 * 30);  // Looper-2-Header
        REQUIRE (Panel::rowCentreYForSlot (specs, 0, 4)
                 - Panel::rowCentreYForSlot (specs, 0, 3) == 2 * 30);  // Busse-Header

        REQUIRE (Panel::heightForSpecs (specs, 0) == 6 + (int) rows.size() * 30 + 6);
    }

    SECTION ("Eingeklappt: Slot-Zeilen entfallen, Kabel-Anker = Überschrift")
    {
        const auto mask = (1 << 0) | (1 << Panel::sectionSends);   // Looper 1 + Sends zu
        const auto rows = Panel::buildRows (specs, mask);

        // 11 Slots − 2 Tracks (Looper 1) − 4 Sends + 4 Überschriften
        REQUIRE (rows.size() == specs.size() - 6 + 4);
        REQUIRE (rows[0].label == "Looper 1");
        REQUIRE (rows[0].collapsed);
        REQUIRE (rows[1].label == "Looper 2");       // direkt danach, keine Tracks

        // Eingeklappte Slots ankern auf ihrer Überschrifts-Zeile (Zeile 0)
        REQUIRE (Panel::rowCentreYForSlot (specs, mask, 0) == 6 + 15);
        REQUIRE (Panel::rowCentreYForSlot (specs, mask, 1) == 6 + 15);
        // Sichtbarer Slot (Looper 2 · Track 5) hinter „Looper 2"-Header
        REQUIRE (Panel::rowCentreYForSlot (specs, mask, 2) == 6 + 2 * 30 + 15);

        REQUIRE (Panel::heightForSpecs (specs, mask)
                 == Panel::heightForSpecs (specs, 0) - 6 * 30);
    }

    SECTION ("sectionOfSpec: Track → Looper-Bit, Bus/Send → 4/5, Master −1")
    {
        REQUIRE (Panel::sectionOfSpec ({ Kind::track, 3, 1, 0 }) == 2);
        REQUIRE (Panel::sectionOfSpec ({ Kind::bus, 2, 0, 0 }) == Panel::sectionBusses);
        REQUIRE (Panel::sectionOfSpec ({ Kind::send, 0, 0, 3 }) == Panel::sectionSends);
        REQUIRE (Panel::sectionOfSpec ({ Kind::master, 0, 0, 0 }) == -1);
    }
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
    auto node = manager.addModuleNode (LooperPatchOutModule::staticModuleId, {});
    REQUIRE (node.isValid());
    settleSwap();
    REQUIRE (node.getProperty (conduit::id::nodeError).toString().isEmpty());

    REQUIRE (node.getChildWithName (conduit::id::outputs).getNumChildren() == 7);
    REQUIRE ((int) node.getProperty (conduit::id::numOutputChannels) == 14);

    const auto patchOutUuid = node.getProperty (conduit::id::nodeId).toString();
    const auto audioOutUuid = engine.getRootState()
        .getChildWithName (conduit::id::nodes)
        .getChildWithProperty (conduit::id::factoryId, juce::String ("audio_output"))
        .getProperty (conduit::id::nodeId).toString();
    REQUIRE (audioOutUuid.isNotEmpty());

    // Master-Slot (Offset 12) an den Hardware-Out verkabeln
    REQUIRE (manager.addConnection (patchOutUuid, 12, audioOutUuid, 0));
    REQUIRE (manager.addConnection (patchOutUuid, 13, audioOutUuid, 1));
    settleSwap();

    const auto masterSourceChannels = [&]
    {
        std::vector<int> channels;
        const auto connections = engine.getRootState().getChildWithName (conduit::id::connections);
        for (int i = 0; i < connections.getNumChildren(); ++i)
            if (const auto connection = connections.getChild (i);
                connection.getProperty (conduit::id::sourceNodeId).toString() == patchOutUuid)
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
        REQUIRE (manager.addConnection (patchOutUuid, 2, audioOutUuid, 0));

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
        stale.setProperty (conduit::id::sourceNodeId, patchOutUuid, nullptr);
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
