#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "Modules/AirwindowsDensityModule.h"
#include "Modules/AirwindowsSpiralModule.h"
#include "Modules/ChassisSchema.h"
#include "Modules/ModuleFactory.h"
#include "UI/CurveEditor.h"
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

TEST_CASE ("FxModulePanel Dev-Modus: User-Range steuert den Fader (extern gesetzt)", "[ui][chassis][devmode]")
{
    ChassisRig rig;
    const auto uuid = rig.node.getProperty (conduit::id::nodeId).toString();

    conduit::FxModulePanel panel { rig.node, rig.manager };
    panel.setDevMode (true);

    // Range-Änderung von außen (Undo/Preset/Kurven-Popup) zieht nach
    REQUIRE (rig.manager.setParameterUserRange (uuid, "density", 0.25, 0.75));
    REQUIRE (panel.columns[0]->slider.getMinimum() == Approx (0.25));
    REQUIRE (panel.columns[0]->slider.getMaximum() == Approx (0.75));

    // Kurven-Button existiert im Dev-Modus (öffnet CurveEditor mit Range)
    REQUIRE (panel.columns[0]->curveButton.isVisible());
}

TEST_CASE ("CurveEditor: Min/Max-Felder committen ueber onRangeChanged, ungueltig restauriert", "[ui][chassis][devmode]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::CurveEditor editor { juce::String(), 0.25, 0.75, 0.0, 1.0 };

    double committedMin = -1.0, committedMax = -1.0;
    bool acceptNext = true;
    editor.onRangeChanged = [&] (double newMin, double newMax)
    {
        committedMin = newMin;
        committedMax = newMax;
        return acceptNext;
    };

    REQUIRE (editor.minEdit.getText().getDoubleValue() == Approx (0.25));

    // Gültiger Commit: Callback bekommt beide Werte, Felder behalten sie
    editor.maxEdit.setText ("0.5", juce::sendNotification);
    REQUIRE (committedMax == Approx (0.5));
    REQUIRE (committedMin == Approx (0.25));
    REQUIRE (editor.maxEdit.getText().getDoubleValue() == Approx (0.5));

    // Abgelehnter Commit (GraphManager sagt nein) → Feld restauriert
    acceptNext = false;
    editor.maxEdit.setText ("0.1", juce::sendNotification);
    REQUIRE (editor.maxEdit.getText().getDoubleValue() == Approx (0.5));
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

TEST_CASE ("CurvedSlider: Bezier-Kurve mappt Position, der Wert bleibt echt", "[ui][chassis][curve]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::CurvedSlider slider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    slider.setRange (0.0, 1.0, 0.0);

    // Linear (keine Kurve): Identität
    REQUIRE (slider.valueToProportionOfLength (0.3) == Approx (0.3));

    const auto curve = conduit::ChassisSchema::parseCurve ("0.9 0.1 0.9 0.4");
    REQUIRE (curve.has_value());
    slider.setResponseCurve (curve);
    REQUIRE (slider.hasResponseCurve());

    // Wert ↔ Position: Roundtrip über die Kurve, Endpunkte bleiben fest
    for (const double value : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        const auto proportion = slider.valueToProportionOfLength (value);
        REQUIRE (slider.proportionOfLengthToValue (proportion) == Approx (value).margin (0.003));
    }

    // Die Kurve VERBIEGT das Mapping tatsächlich (nicht mehr linear)
    REQUIRE (slider.valueToProportionOfLength (0.2) != Approx (0.2).margin (0.05));

    // setValue bleibt der ECHTE Wert (UI-only Mapping)
    slider.setValue (0.3, juce::dontSendNotification);
    REQUIRE (slider.getValue() == Approx (0.3));
}

TEST_CASE ("FxModulePanel: curve-Property erreicht den Spalten-Fader live", "[ui][chassis][curve]")
{
    ChassisRig rig;
    const auto uuid = rig.node.getProperty (conduit::id::nodeId).toString();
    conduit::FxModulePanel panel { rig.node, rig.manager };

    REQUIRE_FALSE (panel.columns[0]->slider.hasResponseCurve());

    REQUIRE (rig.manager.setParameterCurve (uuid, "density", "0.9 0.1 0.9 0.4"));
    REQUIRE (panel.columns[0]->slider.hasResponseCurve());

    // Reset auf linear (Undo-Pfad identisch: Property weg → Kurve weg)
    REQUIRE (rig.manager.setParameterCurve (uuid, "density", ""));
    REQUIRE_FALSE (panel.columns[0]->slider.hasResponseCurve());
}

TEST_CASE ("CurveEditor: Link-Controls committen Quelle + Amount", "[ui][chassis][link]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::CurveEditor editor { juce::String(), 0.0, 1.0, 0.0, 1.0,
                                  { "highpass", "out_level" }, "out_level", -0.5 };

    // Initial: bestehender Link vorausgewählt
    REQUIRE (editor.linkSourceBox.getNumItems() == 3);   // kein Link + 2 Quellen
    REQUIRE (editor.linkSourceBox.getText() == "out_level");
    REQUIRE (editor.linkAmountSlider.getValue() == Approx (-0.5));

    juce::String committedSource ("unberuehrt");
    double committedAmount = 99.0;
    editor.onLinkChanged = [&] (const juce::String& source, double amount)
    {
        committedSource = source;
        committedAmount = amount;
    };

    // Quelle wechseln → Commit mit Quelle + aktuellem Amount
    editor.linkSourceBox.setSelectedItemIndex (1, juce::sendNotificationSync);   // "highpass"
    REQUIRE (committedSource == "highpass");
    REQUIRE (committedAmount == Approx (-0.5));

    // Amount ändern → Commit
    editor.linkAmountSlider.setValue (0.75, juce::sendNotificationSync);
    REQUIRE (committedAmount == Approx (0.75));

    // "kein Link" → leere Quelle (Link lösen)
    editor.linkSourceBox.setSelectedItemIndex (0, juce::sendNotificationSync);
    REQUIRE (committedSource.isEmpty());
}

TEST_CASE ("CurveEditor: Range-Endpunkte sind draggbar, Tabs schalten Fader/Link-Kurve", "[ui][chassis][link]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::CurveEditor editor { juce::String(), 0.2, 0.8, 0.0, 1.0,
                                  { "out_level" }, "out_level", -1.0 };

    double committedMin = -1.0, committedMax = -1.0;
    editor.onRangeChanged = [&] (double newMin, double newMax)
    {
        committedMin = newMin;
        committedMax = newMax;
        return true;
    };

    // Max-Endpunkt nach unten ziehen → userMax folgt, min bleibt
    editor.dragEndpointToValue (true, 0.6);
    REQUIRE (committedMax == Approx (0.6));
    REQUIRE (committedMin == Approx (0.2));
    REQUIRE (editor.maxEdit.getText().getDoubleValue() == Approx (0.6));

    // Min-Endpunkt kann max nie überholen (Mindestabstand)
    editor.dragEndpointToValue (false, 0.9);
    REQUIRE (committedMin < 0.6);

    // Tabs: Link-Tab wählbar (Quelle gesetzt); Kurven-Commits landen getrennt
    juce::String faderCurveText ("unberuehrt"), linkCurveText ("unberuehrt");
    editor.onCurveChanged     = [&] (const juce::String& t) { faderCurveText = t; };
    editor.onLinkCurveChanged = [&] (const juce::String& t) { linkCurveText = t; };

    editor.setActiveTab (conduit::CurveEditor::Tab::link);
    REQUIRE (editor.getActiveTab() == conduit::CurveEditor::Tab::link);
    editor.setHandle (0, 0.9f, 0.1f);
    REQUIRE (linkCurveText.isNotEmpty());
    REQUIRE (faderCurveText == "unberuehrt");

    // Link-Endpunkte: fallende Response (Start 1 → Ende 0) direkt in der
    // Kurve — Quelle hoch, Ziel runter (User-Wunsch Auto-Gain)
    editor.setLinkEndpoint (false, 1.0f);
    editor.setLinkEndpoint (true, 0.0f);
    const auto response = conduit::ChassisSchema::parseLinkResponse (linkCurveText);
    REQUIRE (response.has_value());
    REQUIRE (response->startY == Approx (1.0f));
    REQUIRE (response->endY   == Approx (0.0f));

    editor.setActiveTab (conduit::CurveEditor::Tab::fader);
    editor.setHandle (0, 0.1f, 0.9f);
    REQUIRE (faderCurveText != "unberuehrt");

    // Ohne Quelle ist der Link-Tab gesperrt
    editor.linkSourceBox.setSelectedItemIndex (0, juce::sendNotificationSync);   // kein Link
    editor.setActiveTab (conduit::CurveEditor::Tab::link);
    REQUIRE (editor.getActiveTab() == conduit::CurveEditor::Tab::fader);
}

TEST_CASE ("FxModulePanel: widthForColumns ist die zentrale Breitenformel", "[ui][chassis]")
{
    using Panel = conduit::FxModulePanel;

    REQUIRE (Panel::widthForColumns (0) == 2 * conduit::GainFaderMeter::preferredWidth + 16);
    REQUIRE (Panel::widthForColumns (4) - Panel::widthForColumns (0) == 4 * Panel::columnWidth);
    REQUIRE (Panel::widthForColumns (-1) == Panel::widthForColumns (0));  // defensiv
}

TEST_CASE ("FxModulePanel Button-Modus: Buttons ersetzen den Fader im Normalmodus", "[ui][chassis][buttons]")
{
    ChassisRig rig;
    const auto uuid = rig.node.getProperty (conduit::id::nodeId).toString();

    rig.setParamValue ("density", 0.3);
    REQUIRE (rig.manager.setParameterUiMode (uuid, "density", true));
    REQUIRE (rig.manager.setParameterButtonCount (uuid, "density", 3));
    REQUIRE (rig.manager.renameParameterButton (uuid, "density", 1, "Halb"));

    conduit::FxModulePanel panel { rig.node, rig.manager };
    panel.setSize (panel.getPreferredWidth(), conduit::FxModulePanel::panelHeight);

    // Spalte im Button-Modus: 3 Buttons, Fader unsichtbar, CV bleibt bedienbar
    auto& column = *panel.columns[0];
    REQUIRE (column.buttonMode);
    REQUIRE (column.valueButtons.size() == 3);
    REQUIRE_FALSE (column.slider.isVisible());
    REQUIRE (column.cvKnob.isVisible());
    REQUIRE (column.cvPort != nullptr);
    REQUIRE (column.valueButtons[1]->getText() == "Halb");

    // Andere Spalten bleiben Fader
    REQUIRE_FALSE (panel.columns[1]->buttonMode);
    REQUIRE (panel.columns[1]->slider.isVisible());

    // Recall: Klick schreibt den gespeicherten Wert über den Fader-Pfad
    rig.setParamValue ("density", 0.9);
    REQUIRE_FALSE (column.valueButtons[0]->active);
    column.valueButtons[0]->onClick();
    REQUIRE (rig.paramValue ("density") == Approx (0.3));

    // Aktiv-Markierung folgt dem paramValue (in-place, exactlyEqual)
    REQUIRE (column.valueButtons[0]->active);
    rig.setParamValue ("density", 0.55);
    REQUIRE_FALSE (column.valueButtons[0]->active);
}

TEST_CASE ("FxModulePanel Button-Modus: 6 Buttons -> zwei Stapel, Spalte doppelt breit", "[ui][chassis][buttons]")
{
    ChassisRig rig;
    const auto uuid = rig.node.getProperty (conduit::id::nodeId).toString();

    REQUIRE (rig.manager.setParameterUiMode (uuid, "density", true));
    REQUIRE (rig.manager.setParameterButtonCount (uuid, "density", 6));

    conduit::FxModulePanel panel { rig.node, rig.manager };
    panel.setSize (panel.getPreferredWidth(), conduit::FxModulePanel::panelHeight);

    // Breite: 3 Fader-Spalten + 1 Doppelspalte
    REQUIRE (panel.getPreferredWidth()
             == conduit::FxModulePanel::widthForColumns (3)
                    + 2 * conduit::FxModulePanel::buttonStackWidth);

    // Button 5 (Index 5, zweiter Stapel) liegt rechts von Button 0, gleiche Höhe
    auto& column = *panel.columns[0];
    REQUIRE (column.valueButtons.size() == 6);
    REQUIRE (column.valueButtons[5]->getX() > column.valueButtons[0]->getX());
    REQUIRE (column.valueButtons[5]->getY() == column.valueButtons[0]->getY());

    // Buttons 0..4 stapeln vertikal im ersten Stapel
    REQUIRE (column.valueButtons[4]->getX() == column.valueButtons[0]->getX());
    REQUIRE (column.valueButtons[4]->getY() > column.valueButtons[3]->getY());
}

TEST_CASE ("FxModulePanel Dev+Buttons: Fader UND Buttons, Klick speichert den Fader-Wert undo-faehig", "[ui][chassis][devmode][buttons]")
{
    using S = conduit::ChassisSchema;
    ChassisRig rig;
    const auto uuid = rig.node.getProperty (conduit::id::nodeId).toString();

    rig.setParamValue ("density", 0.3);
    REQUIRE (rig.manager.setParameterUiMode (uuid, "density", true));
    REQUIRE (rig.manager.setParameterButtonCount (uuid, "density", 2));

    conduit::FxModulePanel panel { rig.node, rig.manager };
    panel.setDevMode (true);
    panel.setSize (panel.getPreferredWidth(), conduit::FxModulePanel::panelHeight);

    // Dev: Fader sichtbar UND Buttons daneben
    auto& column = *panel.columns[0];
    REQUIRE (column.slider.isVisible());
    REQUIRE (column.valueButtons.size() == 2);
    REQUIRE (column.valueButtons[0]->getX() >= conduit::FxModulePanel::columnWidth);

    // Kern-Workflow: Fader auf guten Wert, Button-Klick speichert ihn
    column.slider.setValue (0.7, juce::sendNotificationSync);
    panel.columns[0]->valueButtons[1]->onClick();   // Rebuild! Zugriff über Panel

    auto density = rig.node.getChildWithName (conduit::id::parameters)
                       .getChildWithProperty (conduit::id::paramId, "density");
    auto buttons = S::parseButtons (density.getProperty (conduit::id::paramUiButtons).toString());
    REQUIRE ((*buttons)[1].value == Approx (0.7));
    REQUIRE ((*buttons)[0].value == Approx (0.3));

    // Undo restauriert die Liste; Panel rebuildet crashfrei (Friedhof)
    REQUIRE (rig.undoManager.undo());
    buttons = S::parseButtons (density.getProperty (conduit::id::paramUiButtons).toString());
    REQUIRE ((*buttons)[1].value == Approx (0.3));
    REQUIRE (panel.columns[0]->valueButtons.size() == 2);
}

TEST_CASE ("FxModulePanel Dev: modeButton schaltet uiMode, Stepper aendert die Anzahl", "[ui][chassis][devmode][buttons]")
{
    ChassisRig rig;
    conduit::FxModulePanel panel { rig.node, rig.manager };
    panel.setDevMode (true);

    int layoutChanges = 0;
    panel.onLayoutChanged = [&layoutChanges] { ++layoutChanges; };

    // Umschalten auf Buttons: Property + Rebuild + Layout-Callback
    panel.columns[0]->modeButton.onClick();
    auto density = rig.node.getChildWithName (conduit::id::parameters)
                       .getChildWithProperty (conduit::id::paramId, "density");
    REQUIRE (conduit::ChassisSchema::isButtonMode (density));
    REQUIRE (layoutChanges == 1);
    REQUIRE (panel.columns[0]->buttonMode);

    // Stepper: 2× anhängen, 1× entfernen
    panel.columns[0]->addButton.onClick();
    panel.columns[0]->addButton.onClick();
    REQUIRE (panel.columns[0]->valueButtons.size() == 2);
    panel.columns[0]->removeButton.onClick();
    REQUIRE (panel.columns[0]->valueButtons.size() == 1);

    // removeButton bei 0 disabled, addButton bei 10 disabled
    panel.columns[0]->removeButton.onClick();
    REQUIRE (panel.columns[0]->valueButtons.empty());
    REQUIRE_FALSE (panel.columns[0]->removeButton.isEnabled());

    for (int clickIndex = 0; clickIndex < conduit::ChassisSchema::maxUiButtons; ++clickIndex)
        panel.columns[0]->addButton.onClick();
    REQUIRE (static_cast<int> (panel.columns[0]->valueButtons.size())
             == conduit::ChassisSchema::maxUiButtons);
    REQUIRE_FALSE (panel.columns[0]->addButton.isEnabled());

    // Zurück auf Fader: Buttons verschwinden, uiButtons bleibt geparkt
    panel.columns[0]->modeButton.onClick();
    REQUIRE_FALSE (panel.columns[0]->buttonMode);
    REQUIRE (panel.columns[0]->valueButtons.empty());
    REQUIRE (density.hasProperty (conduit::id::paramUiButtons));
    REQUIRE (panel.columns[0]->slider.isVisible());
}

TEST_CASE ("FxModulePanel Dev: Button-Rename via GraphManager erreicht das Label", "[ui][chassis][devmode][buttons]")
{
    ChassisRig rig;
    const auto uuid = rig.node.getProperty (conduit::id::nodeId).toString();

    REQUIRE (rig.manager.setParameterUiMode (uuid, "density", true));
    REQUIRE (rig.manager.setParameterButtonCount (uuid, "density", 1));

    conduit::FxModulePanel panel { rig.node, rig.manager };
    panel.setDevMode (true);

    REQUIRE (panel.columns[0]->valueButtons[0]->getText() == "P1");

    // Rename (Label-Inline-Edit ist headless unzuverlässig — API-Pfad):
    // Property-Change → Rebuild → neues Label trägt den Namen
    REQUIRE (rig.manager.renameParameterButton (uuid, "density", 0, "Sweet"));
    REQUIRE (panel.columns[0]->valueButtons[0]->getText() == "Sweet");

    // Nicht-Dev: Labels sind nicht editierbar
    panel.setDevMode (false);
    REQUIRE_FALSE (panel.columns[0]->valueButtons[0]->isEditableOnDoubleClick());
}

TEST_CASE ("NodeComponent: app-weiter Dev Mode gatet den DEV-Toggle", "[ui][chassis][devmode][uisettings]")
{
    ChassisRig rig;

    // Temp-UiSettings (Muster TempMeterSettings — echte Datei bleibt unberührt)
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ConduitDevGatingTests")
                            .getChildFile (juce::Uuid().toString());
    folder.createDirectory();
    juce::PropertiesFile::Options options;
    options.applicationName = "ConduitDevGatingTests";
    options.filenameSuffix  = ".settings";
    options.folderName      = folder.getFullPathName();

    {
        conduit::UiSettings settings (options);
        REQUIRE_FALSE (settings.isDevModeEnabled());

        // Dev Mode aus → DEV-Button unsichtbar (mit UiSettings injiziert)
        conduit::NodeComponent nodeUi { rig.node, rig.manager, rig.uiRegistry,
                                        nullptr, nullptr, nullptr, nullptr, &settings };
        REQUIRE_FALSE (nodeUi.devButton.isVisible());

        // Aktivieren → sichtbar (Broadcast synchron zustellen)
        settings.setDevModeEnabled (true);
        settings.dispatchPendingMessages();
        REQUIRE (nodeUi.devButton.isVisible());

        // Kachel-Dev-Modus aktivieren, dann global deaktivieren →
        // Button unsichtbar UND Panel-Dev-Modus zurückgesetzt
        nodeUi.devButton.onClick();
        REQUIRE (nodeUi.getFxPanel()->isDevMode());

        settings.setDevModeEnabled (false);
        settings.dispatchPendingMessages();
        REQUIRE_FALSE (nodeUi.devButton.isVisible());
        REQUIRE_FALSE (nodeUi.getFxPanel()->isDevMode());

        // Neuer Node erbt den aktuellen Zustand im ctor
        settings.setDevModeEnabled (true);
        settings.dispatchPendingMessages();
        conduit::NodeComponent fresh { rig.node, rig.manager, rig.uiRegistry,
                                       nullptr, nullptr, nullptr, nullptr, &settings };
        REQUIRE (fresh.devButton.isVisible());

        fresh.completeTeardownNow();
        nodeUi.completeTeardownNow();
    }

    // Ohne UiSettings (Alt-Tests, nullptr): Button sichtbar wie bisher
    conduit::NodeComponent legacy { rig.node, rig.manager, rig.uiRegistry };
    REQUIRE (legacy.devButton.isVisible());
    legacy.completeTeardownNow();

    folder.deleteRecursively();
}

TEST_CASE ("FxModulePanel: columnWidthFor + getPreferredWidth (variable Spaltenbreiten)", "[ui][chassis][buttons]")
{
    using Panel = conduit::FxModulePanel;

    // Fader-Spalte: immer 56, unabhängig von Dev-Modus und Button-Zahl
    REQUIRE (Panel::columnWidthFor (false, false, 0) == Panel::columnWidth);
    REQUIRE (Panel::columnWidthFor (false, true, 7)  == Panel::columnWidth);

    // Button-Spalte (Nicht-Dev): 1 Stapel bis 5 Buttons, danach 2
    REQUIRE (Panel::columnWidthFor (true, false, 0) == Panel::buttonStackWidth);
    REQUIRE (Panel::columnWidthFor (true, false, 5) == Panel::buttonStackWidth);
    REQUIRE (Panel::columnWidthFor (true, false, 6) == 2 * Panel::buttonStackWidth);
    REQUIRE (Panel::columnWidthFor (true, false, 10) == 2 * Panel::buttonStackWidth);

    // Dev+Buttons: Fader-Spalte + Stapel daneben
    REQUIRE (Panel::columnWidthFor (true, true, 3) == Panel::columnWidth + Panel::buttonStackWidth);
    REQUIRE (Panel::columnWidthFor (true, true, 6) == Panel::columnWidth + 2 * Panel::buttonStackWidth);

    // Degeneration: reine Fader-Panels liefern exakt widthForColumns
    ChassisRig rig;
    conduit::FxModulePanel panel { rig.node, rig.manager };
    REQUIRE (panel.getPreferredWidth() == Panel::widthForColumns (panel.getNumColumns()));
}
