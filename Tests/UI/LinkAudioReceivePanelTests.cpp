#include <catch2/catch_test_macros.hpp>

#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/LinkClock.h"
#include "Core/NodeUiRegistry.h"
#include "Modules/LinkAudioReceiveModule.h"
#include "Modules/ModuleFactory.h"
#include "UI/LinkAudioReceivePanel.h"

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

// Voller GraphManager-Rig (Muster SendRig) — das Panel löst das Modul
// transient über den Manager auf.
struct ReceiveRig
{
    ReceiveRig()
    {
        conduit::registerDefaultModules (factory);
        clock.prepare (48000.0);
        manager.setLinkClock (&clock);
        manager.setClockBus (&bus);

        node = manager.addModuleNode (conduit::LinkAudioReceiveModule::staticModuleId, {});
        manager.flushPendingTopologyUpdate();
    }

    [[nodiscard]] conduit::LinkAudioReceiveModule* module() const
    {
        return dynamic_cast<conduit::LinkAudioReceiveModule*> (
            manager.getModuleFor (node.getProperty (conduit::id::nodeId).toString()));
    }

    [[nodiscard]] juce::ValueTree latencyParam() const
    {
        return node.getChildWithName (conduit::id::parameters)
                   .getChildWithProperty (conduit::id::paramId,
                                          juce::String (conduit::LinkAudioReceiveModule::latencyParamId));
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    juce::ValueTree root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };
    conduit::LinkClock clock { 120.0, "ConduitTest" };
    conduit::ClockBus bus;
    juce::ValueTree node;
};

} // namespace

//==============================================================================
TEST_CASE ("LinkAudioReceivePanel: Kanal-Wahl schreibt den Wunsch, Modul rebindet", "[ui][linkaudio][receive]")
{
    ReceiveRig rig;
    conduit::LinkAudioReceivePanel panel { rig.node, rig.manager };

    // Ohne Wunsch: Aufforderungstext, Modul offline
    panel.refreshNow();
    REQUIRE (panel.channelButton.getButtonText().contains (juce::String::fromUTF8 ("wählen")));
    REQUIRE (rig.module()->getReceiveStatusForUi()
             == conduit::LinkAudioReceiveModule::ReceiveStatus::offline);

    // Kanal-Wahl → Tree-Properties → GraphManager spiegelt → Modul sucht
    panel.applyChannelChoice ("Ableton Live", "Track 1");
    REQUIRE (rig.node.getProperty (conduit::id::targetPeer).toString() == "Ableton Live");
    REQUIRE (rig.node.getProperty (conduit::id::targetChannel).toString() == "Track 1");
    REQUIRE (rig.module()->getReceiveStatusForUi()
             == conduit::LinkAudioReceiveModule::ReceiveStatus::searching);
    REQUIRE (panel.channelButton.getButtonText() == "Ableton Live / Track 1");

    // Trennen löst die Bindungssuche
    panel.applyChannelChoice ({}, {});
    REQUIRE (rig.module()->getReceiveStatusForUi()
             == conduit::LinkAudioReceiveModule::ReceiveStatus::offline);

    // Externe Wunsch-Änderung (Undo/Preset-Pfad): Button-Text folgt
    rig.node.setProperty (conduit::id::targetPeer,    "Peer B",  nullptr);
    rig.node.setProperty (conduit::id::targetChannel, "Kanal X", nullptr);
    REQUIRE (panel.channelButton.getButtonText() == "Peer B / Kanal X");
}

//==============================================================================
TEST_CASE ("LinkAudioReceivePanel: Latenz-Slider ↔ paramValue (beide Richtungen)", "[ui][linkaudio][receive]")
{
    ReceiveRig rig;
    conduit::LinkAudioReceivePanel panel { rig.node, rig.manager };

    // Slider → Tree (GraphManager spiegelt aufs Atomic, separat getestet)
    panel.latencySlider.setValue (250.0, juce::sendNotificationSync);
    REQUIRE (juce::exactlyEqual (
        static_cast<double> (rig.latencyParam().getProperty (conduit::id::paramValue)), 250.0));

    // Tree-Spiegelung erreicht das Modul-Atomic (syncParameterValue)
    auto* latencyTarget = rig.module()->getParameterTarget (
        conduit::LinkAudioReceiveModule::latencyParamId);
    REQUIRE (latencyTarget != nullptr);
    REQUIRE (juce::exactlyEqual (latencyTarget->load(), 250.0f));

    // Tree → Slider (Undo/OSC-Pfad), ohne Rückkopplungs-Schleife
    rig.latencyParam().setProperty (conduit::id::paramValue, 90.0, nullptr);
    REQUIRE (juce::exactlyEqual (panel.latencySlider.getValue(), 90.0));

    // stopUpdates (Phase 1): Interaktion aus, keine Tree-Writes mehr
    panel.stopUpdates();
    panel.latencySlider.setValue (400.0, juce::sendNotificationSync);
    REQUIRE (juce::exactlyEqual (
        static_cast<double> (rig.latencyParam().getProperty (conduit::id::paramValue)), 90.0));
    REQUIRE_FALSE (panel.channelButton.isEnabled());
}
