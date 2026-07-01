#include <catch2/catch_test_macros.hpp>

#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "Modules/AttenuatorModule.h"
#include "Modules/ModuleFactory.h"
#include "UI/NodeCanvas.h"

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

constexpr auto attenuatorId = conduit::AttenuatorModule::staticModuleId;

// Headless: Components werden nie auf den Desktop gehängt — der VBlank-Pfad
// feuert daher nicht, Teardowns laufen über completeTeardownNow().
struct UiTestRig
{
    UiTestRig()
    {
        conduit::registerDefaultModules (factory);
    }

    [[nodiscard]] juce::ValueTree nodes() { return root.getChildWithName (conduit::id::nodes); }

    [[nodiscard]] juce::ValueTree gainParameterOf (const juce::ValueTree& node) const
    {
        return node.getChildWithName (conduit::id::parameters)
                   .getChildWithProperty (conduit::id::paramId, "gain");
    }

    static juce::String uuidOf (const juce::ValueTree& node)
    {
        return node.getProperty (conduit::id::nodeId).toString();
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    juce::ValueTree root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };
    conduit::NodeCanvas canvas { root, manager, uiRegistry };
};

} // namespace

//==============================================================================
TEST_CASE ("NodeCanvas: Tree → Components-Sync mit Registry-Refcounts", "[ui]")
{
    UiTestRig rig;
    REQUIRE (rig.canvas.getNumNodeComponents() == 0);

    auto first        = rig.manager.addModuleNode (attenuatorId, { 10, 20 });
    const auto second = rig.manager.addModuleNode (attenuatorId, { 200, 80 });
    REQUIRE (first.isValid());
    REQUIRE (second.isValid());

    REQUIRE (rig.canvas.getNumNodeComponents() == 2);
    REQUIRE (rig.uiRegistry.getRefCount (UiTestRig::uuidOf (first)) == 1);
    REQUIRE (rig.uiRegistry.getRefCount (UiTestRig::uuidOf (second)) == 1);

    // Position: Tree ist führend, Component folgt
    auto* component = rig.canvas.findNodeComponent (UiTestRig::uuidOf (first));
    REQUIRE (component != nullptr);
    REQUIRE (component->getX() == 10);
    REQUIRE (component->getY() == 20);

    first.setProperty (conduit::id::positionX, 99, nullptr);
    REQUIRE (component->getX() == 99);

    // Unbekannte moduleId → keine Patch-Aktion, kein Component
    REQUIRE_FALSE (rig.manager.addModuleNode ("gibt_es_nicht", {}).isValid());
    REQUIRE (rig.canvas.getNumNodeComponents() == 2);
}

//==============================================================================
TEST_CASE ("NodeCanvas: zweiphasiges Delete — UI blockiert Phase 2 bis zur Freigabe (5.3)", "[ui]")
{
    UiTestRig rig;
    const auto node = rig.manager.addModuleNode (attenuatorId, { 10, 10 });
    const auto nodeUuid = UiTestRig::uuidOf (node);
    rig.manager.flushPendingTopologyUpdate();  // materialisieren

    // Phase 1: Component entkoppelt sich, hält die Registry-Referenz aber
    // bis zum Ende des Render-Zyklus
    REQUIRE (rig.manager.requestNodeDelete (nodeUuid));
    auto* component = rig.canvas.findNodeComponent (nodeUuid);
    REQUIRE (component != nullptr);
    REQUIRE (component->isTearingDown());
    REQUIRE (rig.uiRegistry.getRefCount (nodeUuid) == 1);

    // Phase 2 ist blockiert, solange die UI referenziert
    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.nodes().getNumChildren() == 1);

    // Render-Zyklus abgeschlossen (headless: explizit) → Freigabe → Phase 2
    component->completeTeardownNow();
    REQUIRE (rig.canvas.getNumNodeComponents() == 0);
    REQUIRE (rig.uiRegistry.getRefCount (nodeUuid) == 0);

    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.nodes().getNumChildren() == 0);
    REQUIRE (rig.graph.getNumNodes() == 0);
}

//==============================================================================
TEST_CASE ("NodeCanvas: Undo nach Delete restauriert Node samt Component", "[ui]")
{
    UiTestRig rig;
    const auto node = rig.manager.addModuleNode (attenuatorId, { 10, 10 });
    const auto nodeUuid = UiTestRig::uuidOf (node);
    rig.manager.flushPendingTopologyUpdate();

    // Vollständiger Delete-Durchlauf
    REQUIRE (rig.manager.requestNodeDelete (nodeUuid));
    rig.canvas.findNodeComponent (nodeUuid)->completeTeardownNow();
    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.nodes().getNumChildren() == 0);

    // Undo: Subtree kommt mit nodeState == Deleting zurück — erst der Swap
    // setzt Active und zieht damit die Component nach
    REQUIRE (rig.undoManager.undo());
    REQUIRE (rig.canvas.getNumNodeComponents() == 0);  // noch Deleting

    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.nodes().getNumChildren() == 1);
    REQUIRE (rig.canvas.getNumNodeComponents() == 1);
    REQUIRE (rig.uiRegistry.getRefCount (nodeUuid) == 1);
}

//==============================================================================
TEST_CASE ("NodeCanvas: Preset-Load (Container-Austausch) rebuildet ohne Zombies", "[ui]")
{
    UiTestRig rig;
    rig.manager.addModuleNode (attenuatorId, { 10, 10 });
    REQUIRE (rig.canvas.getNumNodeComponents() == 1);

    // "Preset": eigener Root mit zwei Nodes (voller createState-Lifecycle)
    auto preset = makeRootTree();

    for (int i = 0; i < 2; ++i)
        preset.getChildWithName (conduit::id::nodes)
              .appendChild (rig.factory.create (attenuatorId)->createState(), nullptr);

    rig.root.copyPropertiesAndChildrenFrom (preset, nullptr);

    REQUIRE (rig.canvas.getNumNodeComponents() == 2);

    for (int i = 0; i < 2; ++i)
    {
        const auto nodeUuid = UiTestRig::uuidOf (rig.nodes().getChild (i));
        REQUIRE (rig.uiRegistry.getRefCount (nodeUuid) == 1);
        REQUIRE (rig.canvas.findNodeComponent (nodeUuid) != nullptr);
    }
}

//==============================================================================
TEST_CASE ("NodeCanvas: Sequencer-Node bekommt die Grid-Kachel", "[ui]")
{
    UiTestRig rig;
    const auto node = rig.manager.addModuleNode ("sequencer", { 10, 10 });
    REQUIRE (node.isValid());

    auto* component = rig.canvas.findNodeComponent (UiTestRig::uuidOf (node));
    REQUIRE (component != nullptr);
    REQUIRE (component->getWidth() == 492);   // große Kachel mit StepGridDisplay
    REQUIRE (component->getHeight() == 380);  // inkl. Urzwerg-Kontrollleiste
    REQUIRE (rig.uiRegistry.getRefCount (UiTestRig::uuidOf (node)) == 1);

    // Teardown-Flow unverändert (Timer/Listener sauber gestoppt)
    REQUIRE (rig.manager.requestNodeDelete (UiTestRig::uuidOf (node)));
    component->completeTeardownNow();
    REQUIRE (rig.uiRegistry.getRefCount (UiTestRig::uuidOf (node)) == 0);
}

//==============================================================================
TEST_CASE ("NodeComponent: I/O-Ports folgen der Hardware-Kanalzahl (Schritt B)", "[ui][io]")
{
    UiTestRig rig;

    // Externen Endpunkt registrieren (in der App: der AudioGraphIOProcessor)
    const auto graphNode = rig.graph.addNode (std::make_unique<conduit::AttenuatorModule>())->nodeID;
    rig.manager.registerExternalEndpoint (conduit::audioInputModuleId, graphNode);

    // audio_input-Tree-Node von Hand anlegen (wie EngineProcessor::ensureIONodeStates):
    // liefert Kanäle → Ausgangs-Ports, startet stereo
    juce::ValueTree node (conduit::id::node);
    node.setProperty (conduit::id::nodeId,            juce::Uuid().toString(),                    nullptr);
    node.setProperty (conduit::id::factoryId,         conduit::audioInputModuleId,                nullptr);
    node.setProperty (conduit::id::moduleId,          "audio_in",                                 nullptr);
    node.setProperty (conduit::id::nodeState,         conduit::toString (conduit::NodeState::active), nullptr);
    node.setProperty (conduit::id::numInputChannels,  0,                                          nullptr);
    node.setProperty (conduit::id::numOutputChannels, 2,                                          nullptr);
    node.appendChild (juce::ValueTree (conduit::id::parameters), nullptr);
    rig.nodes().appendChild (node, nullptr);

    auto* component = rig.canvas.findNodeComponent (UiTestRig::uuidOf (node));
    REQUIRE (component != nullptr);
    REQUIRE (component->getNumOutputPorts() == 2);
    const auto stereoHeight = component->getHeight();

    SECTION ("Multichannel-Gerätewechsel: 8 Ausgangs-Ports, höhere Kachel")
    {
        node.setProperty (conduit::id::numOutputChannels, 8, nullptr);
        REQUIRE (component->getNumOutputPorts() == 8);
        REQUIRE (component->getNumInputPorts()  == 0);  // andere Bank unberührt
        REQUIRE (component->getHeight() > stereoHeight);
    }

    SECTION ("Schrumpfen zurück auf stereo stellt die Ausgangsgröße wieder her")
    {
        node.setProperty (conduit::id::numOutputChannels, 8, nullptr);
        node.setProperty (conduit::id::numOutputChannels, 2, nullptr);
        REQUIRE (component->getNumOutputPorts() == 2);
        REQUIRE (component->getHeight() == stereoHeight);
    }
}

//==============================================================================
TEST_CASE ("GraphManager: Parameter-Sync Tree → Atomic (UI/Preset/Undo-Pfad)", "[ui]")
{
    UiTestRig rig;

    SECTION ("Live-Änderung nach Materialisierung")
    {
        const auto node = rig.manager.addModuleNode (attenuatorId, {});
        rig.manager.flushPendingTopologyUpdate();

        auto* module = rig.manager.getModuleFor (UiTestRig::uuidOf (node));
        REQUIRE (module != nullptr);
        auto* target = module->getParameterTarget ("gain");
        REQUIRE (target != nullptr);

        rig.gainParameterOf (node).setProperty (conduit::id::paramValue, 0.25, nullptr);
        REQUIRE (juce::exactlyEqual (target->load (std::memory_order_relaxed), 0.25f));

        // Clamping auch auf diesem Pfad
        rig.gainParameterOf (node).setProperty (conduit::id::paramValue, 7.0, nullptr);
        REQUIRE (juce::exactlyEqual (target->load (std::memory_order_relaxed), 1.0f));
    }

    SECTION ("Initial-Sync beim Materialisieren (Preset-Load-Fall)")
    {
        const auto node = rig.manager.addModuleNode (attenuatorId, {});
        rig.gainParameterOf (node).setProperty (conduit::id::paramValue, 0.3, nullptr);

        rig.manager.flushPendingTopologyUpdate();  // erst jetzt materialisiert

        auto* module = rig.manager.getModuleFor (UiTestRig::uuidOf (node));
        REQUIRE (module != nullptr);
        REQUIRE (juce::exactlyEqual (module->getParameterTarget ("gain")->load (std::memory_order_relaxed),
                                     0.3f));
    }
}
