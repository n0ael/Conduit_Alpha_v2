#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "Modules/AirwindowsDensityModule.h"
#include "Modules/AirwindowsSpiralModule.h"
#include "Modules/ChassisSchema.h"
#include "Modules/ModuleFactory.h"
#include "UI/FxModulePanel.h"
#include "UI/NodeComponent.h"

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

// Voller GraphManager-Rig (Panel + Meter-Auflösung brauchen ihn)
struct ChassisRig
{
    explicit ChassisRig (const char* factoryKey = conduit::AirwindowsDensityModule::staticModuleId)
    {
        conduit::registerDefaultModules (factory);
        node = manager.addModuleNode (factoryKey, {});
        manager.flushPendingTopologyUpdate();  // materialisiert das Modul
    }

    [[nodiscard]] double paramValue (const juce::String& paramId) const
    {
        return (double) node.getChildWithName (conduit::id::parameters)
                            .getChildWithProperty (conduit::id::paramId, paramId)
                            .getProperty (conduit::id::paramValue);
    }

    void setParamValue (const juce::String& paramId, double value)
    {
        node.getChildWithName (conduit::id::parameters)
            .getChildWithProperty (conduit::id::paramId, paramId)
            .setProperty (conduit::id::paramValue, value, nullptr);
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    juce::ValueTree root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };
    juce::ValueTree node;
};

} // namespace

//==============================================================================
TEST_CASE ("FxModulePanel: nur DSP-Parameter werden Spalten, Gain-Zuege existieren immer", "[ui][chassis]")
{
    ChassisRig rig;
    conduit::FxModulePanel panel { rig.node, rig.manager };

    // Density: 4 DSP-Spalten (Gains + cv_amt erscheinen NICHT als Spalten)
    REQUIRE (panel.getNumColumns() == 4);
    REQUIRE (panel.columns[0]->paramId == "density");
    REQUIRE (panel.columns[3]->paramId == "dry_wet");

    REQUIRE (panel.inputFader  != nullptr);
    REQUIRE (panel.outputFader != nullptr);
    REQUIRE (panel.inputFader->getParamId()  == juce::String (conduit::ChassisSchema::inputGainId));
    REQUIRE (panel.outputFader->getParamId() == juce::String (conduit::ChassisSchema::outputGainId));

    // Gain-Fader in dB-Range
    REQUIRE (panel.inputFader->slider.getMinimum() == Approx (-60.0));
    REQUIRE (panel.inputFader->slider.getMaximum() == Approx (6.0));
}

TEST_CASE ("FxModulePanel: Spiral hat 0 Spalten, aber beide Gain-Zuege", "[ui][chassis]")
{
    ChassisRig rig { conduit::AirwindowsSpiralModule::staticModuleId };
    conduit::FxModulePanel panel { rig.node, rig.manager };

    REQUIRE (panel.getNumColumns() == 0);
    REQUIRE (panel.inputFader  != nullptr);
    REQUIRE (panel.outputFader != nullptr);
}

//==============================================================================
TEST_CASE ("FxModulePanel: Fader schreiben paramValue, externe Aenderungen ziehen nach", "[ui][chassis]")
{
    ChassisRig rig;
    conduit::FxModulePanel panel { rig.node, rig.manager };

    // Spalten-Fader → Tree
    panel.columns[0]->slider.setValue (0.7, juce::sendNotificationSync);
    REQUIRE (rig.paramValue ("density") == Approx (0.7));

    // Gain-Fader → Tree
    panel.outputFader->slider.setValue (-12.0, juce::sendNotificationSync);
    REQUIRE (rig.paramValue ("output_gain") == Approx (-12.0));

    // Tree → Fader (OSC-Nachzug/Undo/Preset-Load)
    rig.setParamValue ("density", 0.2);
    REQUIRE (panel.columns[0]->slider.getValue() == Approx (0.2));

    rig.setParamValue ("input_gain", -6.0);
    REQUIRE (panel.inputFader->slider.getValue() == Approx (-6.0));
}

TEST_CASE ("FxModulePanel: stopUpdates deaktiviert alle Fader (Phase 1)", "[ui][chassis]")
{
    ChassisRig rig;
    conduit::FxModulePanel panel { rig.node, rig.manager };

    panel.stopUpdates();

    REQUIRE_FALSE (panel.columns[0]->slider.isEnabled());
    REQUIRE_FALSE (panel.inputFader->slider.isEnabled());
    REQUIRE_FALSE (panel.outputFader->slider.isEnabled());

    // Nach stopUpdates zieht der Tree nicht mehr nach (Listener entfernt)
    const auto before = panel.columns[0]->slider.getValue();
    rig.setParamValue ("density", 0.9);
    REQUIRE (panel.columns[0]->slider.getValue() == Approx (before));
}

//==============================================================================
TEST_CASE ("GainFaderMeter: ohne materialisiertes Modul kein Crash (Zombie-Regel)", "[ui][chassis]")
{
    ChassisRig rig;

    // Node-Tree, der im Manager NICHT existiert → getModuleFor == nullptr
    conduit::AirwindowsDensityModule detached;
    auto orphanTree = detached.createState();

    conduit::GainFaderMeter fader { orphanTree, conduit::ChassisSchema::inputGainId,
                                    rig.manager, true };
    fader.setBounds (0, 0, 60, 200);

    // Paint-Durchlauf auf ein Image — resolveMeter() liefert nullptr, kein Crash
    juce::Image image (juce::Image::ARGB, 60, 200, true);
    juce::Graphics g (image);
    fader.paint (g);

    fader.stopUpdates();
}

//==============================================================================
TEST_CASE ("NodeComponent: Processor-Nodes bekommen das FxModulePanel", "[ui][chassis]")
{
    ChassisRig rig;
    conduit::NodeComponent nodeUi { rig.node, rig.manager, rig.uiRegistry };

    REQUIRE (nodeUi.hasFxPanel());
    REQUIRE (nodeUi.getFxPanel()->getNumColumns() == 4);

    // Breite folgt der Spaltenzahl (zentrale Formel)
    REQUIRE (nodeUi.getWidth()
             == juce::jmax (conduit::NodeComponent::defaultWidth,
                            conduit::FxModulePanel::widthForColumns (4) + 56));

    nodeUi.completeTeardownNow();
}

TEST_CASE ("FxModulePanel: CV-Knobs binden {param}_cv_amt, Ports tragen Kanal 2+i", "[ui][chassis]")
{
    ChassisRig rig;
    conduit::FxModulePanel panel { rig.node, rig.manager };

    // Attenuverter bipolar −1..+1, Default 0
    REQUIRE (panel.columns[0]->cvKnob.getMinimum() == Approx (-1.0));
    REQUIRE (panel.columns[0]->cvKnob.getMaximum() == Approx (1.0));
    REQUIRE (panel.columns[0]->cvKnob.getValue()   == Approx (0.0));

    // Knob → Tree
    panel.columns[0]->cvKnob.setValue (0.5, juce::sendNotificationSync);
    REQUIRE (rig.paramValue ("density_cv_amt") == Approx (0.5));

    // Tree → Knob (OSC-Nachzug/Undo/Preset-Load)
    rig.setParamValue ("highpass_cv_amt", -0.75);
    REQUIRE (panel.columns[1]->cvKnob.getValue() == Approx (-0.75));

    // CV-Ports: Input-Ports mit festem Kanal-Layout (Audio 0..1, CV 2..N)
    for (int i = 0; i < panel.getNumColumns(); ++i)
    {
        const auto* port = panel.getCvPort (i);
        REQUIRE (port != nullptr);
        REQUIRE (port->getInfo().isInput);
        REQUIRE (port->getInfo().channel == conduit::FxModulePanel::firstCvChannel + i);
        REQUIRE (port->getInfo().span == 1);
    }
}

TEST_CASE ("NodeComponent: CV-Anker liegen im Panel, Kante traegt nur Audio-Ports", "[ui][chassis]")
{
    ChassisRig rig;
    conduit::NodeComponent nodeUi { rig.node, rig.manager, rig.uiRegistry };

    // Linke Kante: nur die 2 Audio-Eingänge (CV-Ports leben im Panel)
    REQUIRE (nodeUi.getNumInputPorts() == 2);
    REQUIRE (nodeUi.getNumOutputPorts() == 2);

    // CV-Anker (Kanal 2..5) liegen rechts der Kanten-Ports im Panel-Bereich
    for (int channel = 2; channel <= 5; ++channel)
    {
        const auto centre = nodeUi.getPortCentre (true, channel);
        REQUIRE (centre.x > 40);
        REQUIRE (centre.y > conduit::NodeComponent::touchTarget);
        REQUIRE (centre.x < nodeUi.getWidth());
        REQUIRE (centre.y < nodeUi.getHeight());

        // Drop-Ziel: findPortNear findet den CV-Port an seinem Anker
        const auto* port = nodeUi.findPortNear (centre, 10);
        REQUIRE (port != nullptr);
        REQUIRE (port->getInfo().channel == channel);
        REQUIRE (port->getInfo().isInput);
    }

    // Audio-Anker bleiben an der Kante
    REQUIRE (nodeUi.getPortCentre (true, 0).x == 12);
    REQUIRE (nodeUi.getPortCentre (false, 0).x == nodeUi.getWidth() - 12);

    nodeUi.completeTeardownNow();
}

TEST_CASE ("FxModulePanel: stopUpdates deaktiviert auch die CV-Knobs", "[ui][chassis]")
{
    ChassisRig rig;
    conduit::FxModulePanel panel { rig.node, rig.manager };

    panel.stopUpdates();
    REQUIRE_FALSE (panel.columns[0]->cvKnob.isEnabled());
}

TEST_CASE ("FxModulePanel: LINK-Button toggelt linkSendEnabled undo-faehig", "[ui][chassis]")
{
    ChassisRig rig;
    conduit::FxModulePanel panel { rig.node, rig.manager };

    REQUIRE_FALSE ((bool) rig.node.getProperty (conduit::id::linkSendEnabled));
    REQUIRE_FALSE (panel.linkSendButton.getToggleState());

    // Klick → Patch-Property (undo-fähig), Button-Zustand folgt dem Tree
    // (onClick direkt — triggerClick dispatched async, headless unzuverlässig)
    panel.linkSendButton.onClick();
    REQUIRE ((bool) rig.node.getProperty (conduit::id::linkSendEnabled));
    REQUIRE (panel.linkSendButton.getToggleState());

    // Undo nimmt den Toggle zurück — UI zieht über den Listener nach
    REQUIRE (rig.undoManager.undo());
    REQUIRE_FALSE ((bool) rig.node.getProperty (conduit::id::linkSendEnabled));
    REQUIRE_FALSE (panel.linkSendButton.getToggleState());

    // LED-Status ohne Link-Kontext: offline, refresh crashfrei
    panel.refreshSendStatusNow();
    REQUIRE (panel.getShownSendStatus() == conduit::LinkSendTaps::Status::offline);
}

TEST_CASE ("FxModulePanel Dev-Modus: uiHidden-Spalten verschwinden nur im Normalmodus", "[ui][chassis][devmode]")
{
    ChassisRig rig;
    REQUIRE (rig.manager.setParameterHidden (
        rig.node.getProperty (conduit::id::nodeId).toString(), "density", true));

    conduit::FxModulePanel panel { rig.node, rig.manager };
    panel.setSize (conduit::FxModulePanel::widthForColumns (4), conduit::FxModulePanel::panelHeight);

    // Normalmodus: 3 sichtbare Spalten, density fehlt komplett
    REQUIRE (panel.getNumColumns() == 3);
    REQUIRE (panel.columns[0]->paramId == "highpass");
    REQUIRE (panel.cvPortCentre (2) == juce::Point<int>());   // kein Anker für hidden

    // Kanal-Zuordnung bleibt fest: highpass ankert weiter auf Kanal 3
    REQUIRE (panel.columns[0]->cvChannel == 3);

    // Dev-Modus: alle 4 Spalten sichtbar, hidden ohne Port + markiert
    int layoutChanges = 0;
    panel.onLayoutChanged = [&layoutChanges] { ++layoutChanges; };
    panel.setDevMode (true);

    REQUIRE (layoutChanges == 1);
    REQUIRE (panel.getNumColumns() == 4);
    REQUIRE (panel.columns[0]->paramId == "density");
    REQUIRE (panel.columns[0]->hidden);
    REQUIRE (panel.columns[0]->cvPort == nullptr);
    REQUIRE (panel.columns[1]->cvPort != nullptr);

    // Einblenden über den Dev-Toggle: Spalte kehrt mit Port zurück
    panel.columns[0]->hideButton.onClick();
    REQUIRE (panel.getNumColumns() == 4);
    REQUIRE_FALSE (panel.columns[0]->hidden);
    REQUIRE (panel.columns[0]->cvPort != nullptr);
    REQUIRE (layoutChanges == 2);   // uiHidden-Listener → rebuild
}

TEST_CASE ("FxModulePanel Dev-Modus: User-Range steuert den Fader, Editierfelder committen", "[ui][chassis][devmode]")
{
    ChassisRig rig;
    const auto uuid = rig.node.getProperty (conduit::id::nodeId).toString();

    conduit::FxModulePanel panel { rig.node, rig.manager };
    panel.setDevMode (true);

    // Range-Änderung von außen (Undo/Preset/zweites Panel) zieht nach
    REQUIRE (rig.manager.setParameterUserRange (uuid, "density", 0.25, 0.75));
    REQUIRE (panel.columns[0]->slider.getMinimum() == Approx (0.25));
    REQUIRE (panel.columns[0]->slider.getMaximum() == Approx (0.75));
    REQUIRE (panel.columns[0]->minEdit.getText().getDoubleValue() == Approx (0.25));

    // Editierfeld committet über den GraphManager (undo-fähig)
    panel.columns[0]->maxEdit.setText ("0.5", juce::sendNotification);
    auto density = rig.node.getChildWithName (conduit::id::parameters)
                       .getChildWithProperty (conduit::id::paramId, "density");
    REQUIRE ((double) density.getProperty (conduit::id::paramUserMax) == Approx (0.5));
    REQUIRE (panel.columns[0]->slider.getMaximum() == Approx (0.5));

    // Ungültige Eingabe wird abgelehnt und aus dem Tree restauriert
    panel.columns[0]->maxEdit.setText ("0.1", juce::sendNotification);   // < userMin
    REQUIRE ((double) density.getProperty (conduit::id::paramUserMax) == Approx (0.5));
    REQUIRE (panel.columns[0]->maxEdit.getText().getDoubleValue() == Approx (0.5));
}

TEST_CASE ("NodeComponent: DEV-Toggle schaltet das Panel, Breite folgt den Spalten", "[ui][chassis][devmode]")
{
    ChassisRig rig;
    const auto uuid = rig.node.getProperty (conduit::id::nodeId).toString();
    REQUIRE (rig.manager.setParameterHidden (uuid, "density", true));

    conduit::NodeComponent nodeUi { rig.node, rig.manager, rig.uiRegistry };

    // Normalmodus: Breite folgt den 3 sichtbaren Spalten
    REQUIRE (nodeUi.getWidth() == conduit::FxModulePanel::widthForColumns (3) + 56);
    REQUIRE_FALSE (nodeUi.getFxPanel()->isDevMode());

    // DEV-Klick: Panel in den Dev-Modus, Breite wächst auf alle 4 Spalten
    nodeUi.devButton.onClick();
    REQUIRE (nodeUi.getFxPanel()->isDevMode());
    REQUIRE (nodeUi.getWidth() == conduit::FxModulePanel::widthForColumns (4) + 56);

    nodeUi.completeTeardownNow();
}

TEST_CASE ("FxModulePanel: widthForColumns ist die zentrale Breitenformel", "[ui][chassis]")
{
    using Panel = conduit::FxModulePanel;

    REQUIRE (Panel::widthForColumns (0) == 2 * conduit::GainFaderMeter::preferredWidth + 16);
    REQUIRE (Panel::widthForColumns (4) - Panel::widthForColumns (0) == 4 * Panel::columnWidth);
    REQUIRE (Panel::widthForColumns (-1) == Panel::widthForColumns (0));  // defensiv
}
