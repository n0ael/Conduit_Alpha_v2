#include <catch2/catch_test_macros.hpp>

#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/LinkClock.h"
#include "Core/NodeUiRegistry.h"
#include "Modules/LinkAudioSendModule.h"
#include "Modules/ModuleFactory.h"
#include "UI/LinkAudioSendPanel.h"
#include "UI/LinkSendCreateDialog.h"

namespace
{

using Mode = conduit::LinkAudioSendModule::InputMode;

juce::ValueTree makeRootTree()
{
    juce::ValueTree root (conduit::id::root);
    root.appendChild (juce::ValueTree (conduit::id::nodes),               nullptr);
    root.appendChild (juce::ValueTree (conduit::id::connections),         nullptr);
    root.appendChild (juce::ValueTree (conduit::id::calibrationProfiles), nullptr);
    return root;
}

// Voller GraphManager-Rig (das Panel braucht ihn für Refresh + Slot-Status)
struct SendRig
{
    SendRig (const std::vector<Mode>& modes)
    {
        conduit::registerDefaultModules (factory);
        clock.prepare (48000.0);
        manager.setLinkClock (&clock);
        manager.setClockBus (&bus);

        sendNode = manager.addModuleNode (conduit::LinkAudioSendModule::staticModuleId, {},
            [&modes] (juce::ValueTree& t) { conduit::LinkAudioSendModule::applyInputConfig (t, modes); });
        manager.flushPendingTopologyUpdate();
    }

    [[nodiscard]] juce::String sendUuid() const { return sendNode.getProperty (conduit::id::nodeId).toString(); }

    [[nodiscard]] juce::ValueTree inputAt (int i) const
    {
        return sendNode.getChildWithName (conduit::id::inputs).getChild (i);
    }

    [[nodiscard]] float gainOf (int i) const
    {
        const auto id = inputAt (i).getProperty (conduit::id::inputGainParamId).toString();
        return static_cast<float> ((double) sendNode.getChildWithName (conduit::id::parameters)
                                       .getChildWithProperty (conduit::id::paramId, id)
                                       .getProperty (conduit::id::paramValue));
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
    juce::ValueTree sendNode;
};

} // namespace

//==============================================================================
TEST_CASE ("LinkAudioSendPanel: eine Zeile pro Eingang, Attenuator schreibt paramValue", "[ui][linkaudio]")
{
    SendRig rig ({ Mode::mono, Mode::stereo });
    conduit::LinkAudioSendPanel panel { rig.sendNode, rig.manager };

    REQUIRE (panel.getNumRows() == 2);

    // Attenuator → Tree (GraphManager spiegelt aufs Atomic, separat getestet)
    panel.rows[0]->gainSlider.setValue (0.5, juce::sendNotificationSync);
    REQUIRE (juce::exactlyEqual (rig.gainOf (0), 0.5f));

    panel.rows[1]->gainSlider.setValue (0.25, juce::sendNotificationSync);
    REQUIRE (juce::exactlyEqual (rig.gainOf (1), 0.25f));
}

//==============================================================================
TEST_CASE ("LinkAudioSendPanel: Name-Editor schreibt userName (leer = zurueck zu auto)", "[ui][linkaudio]")
{
    SendRig rig ({ Mode::stereo });
    conduit::LinkAudioSendPanel panel { rig.sendNode, rig.manager };

    panel.rows[0]->nameLabel.setText ("kick", juce::sendNotificationSync);
    REQUIRE (rig.inputAt (0).getProperty (conduit::id::inputUserName).toString() == "kick");

    // live auf den Sink (Praefix {moduleId}/kick)
    auto* module = dynamic_cast<conduit::LinkAudioSendModule*> (rig.manager.getModuleFor (rig.sendUuid()));
    REQUIRE (module != nullptr);
    const auto moduleId = rig.sendNode.getProperty (conduit::id::moduleId).toString();
    REQUIRE (module->getSinkNames() == juce::StringArray (moduleId + "/kick"));

    // leer → zurueck zum Auto-/Default-Namen
    panel.rows[0]->nameLabel.setText ("", juce::sendNotificationSync);
    REQUIRE (rig.inputAt (0).getProperty (conduit::id::inputUserName).toString().isEmpty());
    REQUIRE (module->getSinkNames() == juce::StringArray (moduleId + "/input1"));
}

//==============================================================================
TEST_CASE ("LinkAudioSendPanel: Refresh-Knopf zieht autoName aus der Quelle, Label folgt", "[ui][linkaudio]")
{
    SendRig rig ({ Mode::stereo });
    conduit::LinkAudioSendPanel panel { rig.sendNode, rig.manager };

    const auto lfo = rig.manager.addModuleNode ("lfo", {});
    rig.manager.flushPendingTopologyUpdate();
    const auto lfoId = lfo.getProperty (conduit::id::moduleId).toString();

    // Verbinden setzt autoName bereits per Snapshot; das Label folgt extern
    REQUIRE (rig.manager.addConnection (lfo.getProperty (conduit::id::nodeId).toString(), 0,
                                        rig.sendUuid(), 0));
    REQUIRE (panel.rows[0]->nameLabel.getText() == lfoId);

    // Refresh-Knopf ist idempotent gegenueber derselben Quelle
    REQUIRE (panel.refreshButton.onClick != nullptr);
    panel.refreshButton.onClick();
    REQUIRE (rig.inputAt (0).getProperty (conduit::id::inputAutoName).toString() == lfoId);
    REQUIRE (panel.rows[0]->nameLabel.getText() == lfoId);
}

//==============================================================================
TEST_CASE ("LinkSendCreateDialog: buildModes = Monos dann Stereos, mind. 1 Eingang", "[ui][linkaudio]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LinkSendCreateDialog dialog;

    // Default: 0 mono / 1 stereo
    auto modes = dialog.buildModes();
    REQUIRE (modes.size() == 1);
    REQUIRE (modes[0] == Mode::stereo);

    // 2 mono + 1 stereo → Reihenfolge mono, mono, stereo
    dialog.monoCount = 2;
    dialog.stereoCount = 1;
    modes = dialog.buildModes();
    REQUIRE (modes.size() == 3);
    REQUIRE (modes[0] == Mode::mono);
    REQUIRE (modes[1] == Mode::mono);
    REQUIRE (modes[2] == Mode::stereo);

    // Minus-Button klemmt bei 0 und garantiert mind. einen Eingang
    dialog.monoCount = 0;
    dialog.stereoCount = 1;
    dialog.stereoMinus.onClick();          // 1 -> 0, Sicherheitsnetz greift
    modes = dialog.buildModes();
    REQUIRE_FALSE (modes.empty());
}
