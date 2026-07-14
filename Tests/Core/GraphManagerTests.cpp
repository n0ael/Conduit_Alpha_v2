#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "Modules/AttenuatorModule.h"
#include "Modules/ModuleFactory.h"
#include "Modules/UtilityModule.h"

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

juce::ValueTree makeModuleNode (const juce::String& moduleId)
{
    juce::ValueTree node (conduit::id::node);
    node.setProperty (conduit::id::nodeId, juce::Uuid().toString(), nullptr);
    node.setProperty (conduit::id::moduleId, moduleId, nullptr);
    return node;
}

juce::ValueTree makeConnection (const juce::ValueTree& source, int sourceChannel,
                                const juce::ValueTree& dest, int destChannel)
{
    juce::ValueTree connection (conduit::id::connection);
    connection.setProperty (conduit::id::sourceNodeId,  source.getProperty (conduit::id::nodeId), nullptr);
    connection.setProperty (conduit::id::sourceChannel, sourceChannel, nullptr);
    connection.setProperty (conduit::id::destNodeId,    dest.getProperty (conduit::id::nodeId), nullptr);
    connection.setProperty (conduit::id::destChannel,   destChannel, nullptr);
    return connection;
}

void pumpUntilSilent (conduit::GraphFader& fader)
{
    juce::AudioBuffer<float> buffer (2, 32);

    for (int i = 0; i < 100 && ! fader.isFadeOutComplete(); ++i)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (channel),
                                               1.0f, buffer.getNumSamples());
        fader.process (buffer);
    }
}

// Stub für den nodeError-Pfad: prepareForGraph schlägt kontrolliert fehl
class FailingModule final : public conduit::UtilityModule
{
public:
    FailingModule()
        : conduit::UtilityModule (BusesProperties()
              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    juce::String getModuleId() const override          { return "failing_test_module"; }
    juce::String getModuleDisplayName() const override { return "Failing Test Module"; }
    int getStateVersion() const override               { return 1; }

    juce::Result prepareForGraph (double, int) override
    {
        return juce::Result::fail ("Allokation fehlgeschlagen (Test)");
    }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
};

constexpr auto attenuatorId = conduit::AttenuatorModule::staticModuleId;

// Gemeinsames Setup: GraphManager mit allen Abhängigkeiten
struct TestRig
{
    TestRig()
    {
        conduit::registerDefaultModules (factory);
        factory.registerModule ({ "failing_test_module", "Failing Test Module",
                                  conduit::ModuleDescriptor::Branch::cvControl,
                                  "Test", {} },
                                [] { return std::make_unique<FailingModule>(); });
    }

    [[nodiscard]] juce::ValueTree nodes()       { return root.getChildWithName (conduit::id::nodes); }
    [[nodiscard]] juce::ValueTree connections() { return root.getChildWithName (conduit::id::connections); }

    juce::ValueTree addModuleNode (const juce::String& moduleId)
    {
        auto node = makeModuleNode (moduleId);
        nodes().appendChild (node, nullptr);
        return node;
    }

    static juce::String uuidOf (const juce::ValueTree& node)
    {
        return node.getProperty (conduit::id::nodeId).toString();
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    juce::ValueTree root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;  // unprepared → Swap ohne Fade, außer fader.prepare() wird gerufen
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };
};

} // namespace

//==============================================================================
TEST_CASE ("Batch-Coalescing: viele Topologie-Änderungen in einem Frame ergeben einen Rebuild",
           "[GraphManager]")
{
    TestRig rig;

    // Bulk-Szenario aus CLAUDE.md 5.5: 5 Module + 20 Kabel in einem Frame
    for (int i = 0; i < 5; ++i)
        rig.addModuleNode (attenuatorId);

    for (int i = 0; i < 20; ++i)
        rig.connections().appendChild (juce::ValueTree (conduit::id::connection), nullptr);

    REQUIRE (rig.manager.isTopologyDirty());
    REQUIRE (rig.manager.getRebuildCount() == 0);  // noch kein Loop-Durchlauf

    rig.manager.flushPendingTopologyUpdate();      // = nächster Message-Loop-Durchlauf

    CHECK (rig.manager.getRebuildCount() == 1);    // 25 Änderungen → genau 1 Rebuild
    CHECK_FALSE (rig.manager.isTopologyDirty());
    CHECK (rig.graph.getNumNodes() == 5);          // alle 5 Module materialisiert

    // Folge-Durchlauf ohne neue Änderungen darf nichts tun
    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.manager.getRebuildCount() == 1);
}

//==============================================================================
TEST_CASE ("Parameter-Änderungen lösen keinen Graph-Rebuild aus", "[GraphManager]")
{
    TestRig rig;

    auto node = rig.addModuleNode (attenuatorId);
    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.manager.getRebuildCount() == 1);

    // Parameters-Subtree unterhalb eines Nodes ist KEINE Topologie-Änderung
    juce::ValueTree parameters (conduit::id::parameters);
    juce::ValueTree parameter (conduit::id::parameter);
    parameters.appendChild (parameter, nullptr);
    node.appendChild (parameters, nullptr);

    // OSC-Dual-State-Pfad (6.1): Property-Updates im Millisekundentakt
    for (int i = 0; i < 100; ++i)
        parameter.setProperty (conduit::id::paramValue, i * 0.01, nullptr);

    CHECK_FALSE (rig.manager.isTopologyDirty());
    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.manager.getRebuildCount() == 1);    // unverändert
}

//==============================================================================
TEST_CASE ("Preset-Load: Container-Austausch ergibt einen einzigen Rebuild", "[GraphManager]")
{
    TestRig rig;

    // Geladenes Preset mit eigener Topologie
    auto loaded = makeRootTree();
    auto loadedNodes = loaded.getChildWithName (conduit::id::nodes);
    for (int i = 0; i < 3; ++i)
        loadedNodes.appendChild (makeModuleNode (attenuatorId), nullptr);

    // Preset-Load-Pfad aus EngineProcessor::setStateInformation():
    // Container werden als ganze Subtrees ersetzt (parent ist der Root)
    rig.root.copyPropertiesAndChildrenFrom (loaded, nullptr);

    REQUIRE (rig.manager.isTopologyDirty());
    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.manager.getRebuildCount() == 1);
    CHECK (rig.graph.getNumNodes() == 3);
}

//==============================================================================
TEST_CASE ("Graph-Mutationen: Tree-Nodes werden zu Graph-Nodes (add/remove)",
           "[GraphManager]")
{
    TestRig rig;

    auto attenuator = rig.addModuleNode (attenuatorId);

    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.graph.getNumNodes() == 1);
    CHECK (attenuator.getProperty (conduit::id::nodeError).toString().isEmpty());

    rig.nodes().removeChild (attenuator, nullptr);
    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.graph.getNumNodes() == 0);
}

//==============================================================================
TEST_CASE ("Graph-Mutationen: Connections werden aus dem Tree synchronisiert",
           "[GraphManager]")
{
    TestRig rig;

    auto source = rig.addModuleNode (attenuatorId);
    auto dest   = rig.addModuleNode (attenuatorId);

    // Stereo-Kabel: L→L, R→R
    auto left  = makeConnection (source, 0, dest, 0);
    auto right = makeConnection (source, 1, dest, 1);
    rig.connections().appendChild (left, nullptr);
    rig.connections().appendChild (right, nullptr);

    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.graph.getNumNodes() == 2);
    CHECK (rig.graph.getConnections().size() == 2u);

    // Kabel ziehen: ein Connection-Child entfernen
    rig.connections().removeChild (right, nullptr);
    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.graph.getConnections().size() == 1u);

    // Node löschen reißt auch sein verbliebenes Kabel mit
    rig.nodes().removeChild (dest, nullptr);
    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.graph.getNumNodes() == 1);
    CHECK (rig.graph.getConnections().size() == 0u);
}

//==============================================================================
TEST_CASE ("Async Prepare: unbekannte moduleId → nodeError, kein Graph-Node, kein Retry",
           "[GraphManager]")
{
    TestRig rig;

    auto unknown = rig.addModuleNode ("gibts_nicht");

    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.graph.getNumNodes() == 0);
    CHECK (unknown.getProperty (conduit::id::nodeError).toString().isNotEmpty());

    // Kein Retry-Loop: der nächste Swap überspringt den Fehler-Node,
    // gesunde Module sind nicht betroffen
    rig.addModuleNode (attenuatorId);
    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.graph.getNumNodes() == 1);
}

//==============================================================================
TEST_CASE ("Async Prepare: fehlschlagendes prepareForGraph → nodeError, Modul nicht im Graph",
           "[GraphManager]")
{
    TestRig rig;

    auto failing = rig.addModuleNode ("failing_test_module");

    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.graph.getNumNodes() == 0);
    CHECK (failing.getProperty (conduit::id::nodeError).toString()
               == "Allokation fehlgeschlagen (Test)");
}

//==============================================================================
TEST_CASE ("Graph-Swap: vollständiger Fade-Zyklus nach CLAUDE.md 5.2", "[GraphManager]")
{
    TestRig rig;
    rig.fader.prepare (48000.0);  // Audio "läuft" → Swap nur mit Fade-Zyklus

    rig.addModuleNode (attenuatorId);
    REQUIRE (rig.manager.isTopologyDirty());

    // Schritt 1 + 2: erster Loop-Durchlauf prepariert das Modul und startet
    // den Fade-Out — die Topologie wird noch NICHT geändert
    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.manager.getRebuildCount() == 0);
    CHECK (rig.graph.getNumNodes() == 0);
    CHECK (rig.manager.isWaitingForSilence());
    CHECK (rig.fader.getCurrentPhase() == conduit::GraphFader::Phase::fadingOut);

    // Schritt 3 (Self-Re-Dispatch): solange keine Stille gemeldet ist,
    // findet kein Swap statt
    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.manager.getRebuildCount() == 0);

    // Coalescing während des Fade-Outs: neue Änderungen landen im selben Swap
    rig.addModuleNode (attenuatorId);
    rig.addModuleNode (attenuatorId);

    // Audio Thread rampt auf Stille
    pumpUntilSilent (rig.fader);
    REQUIRE (rig.fader.isFadeOutComplete());

    // Schritt 3: Topologie-Swap auf Stille, dann Schritt 4: Fade-In
    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.manager.getRebuildCount() == 1);   // 3 Änderungen → 1 Swap
    CHECK (rig.graph.getNumNodes() == 3);         // alle 3 Module im Graph
    CHECK_FALSE (rig.manager.isWaitingForSilence());
    CHECK_FALSE (rig.manager.isTopologyDirty());
    CHECK (rig.fader.getCurrentPhase() == conduit::GraphFader::Phase::fadingIn);
}

//==============================================================================
TEST_CASE ("Graph-Swap: gestopptes Audio während des Fade-Outs blockiert den Swap nicht",
           "[GraphManager]")
{
    TestRig rig;
    rig.fader.prepare (48000.0);

    rig.addModuleNode (attenuatorId);

    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.manager.isWaitingForSilence());

    // Audio stoppt mitten im Fade-Out (EngineProcessor::releaseResources) —
    // der Audio Thread wird nie Stille melden
    rig.fader.reset();

    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.manager.getRebuildCount() == 1);   // Swap ohne Fade statt Endlos-Dispatch
    CHECK (rig.graph.getNumNodes() == 1);
    CHECK_FALSE (rig.manager.isWaitingForSilence());
}

//==============================================================================
TEST_CASE ("Zweiphasiges Delete (5.3): Phase 1 → Deleting, Phase 2 entfernt Node + Kabel undo-fähig",
           "[GraphManager]")
{
    TestRig rig;

    auto source = rig.addModuleNode (attenuatorId);
    auto dest   = rig.addModuleNode (attenuatorId);
    rig.connections().appendChild (makeConnection (source, 0, dest, 0), nullptr);

    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.graph.getNumNodes() == 2);
    REQUIRE (rig.graph.getConnections().size() == 1u);

    const auto sourceUuid = TestRig::uuidOf (source);
    REQUIRE (rig.manager.requestNodeDelete (sourceUuid));

    // Phase 1: nodeState → Deleting, Node und Graph noch unverändert —
    // UI-Components entkoppeln sich jetzt über ihren Listener
    CHECK (source.getProperty (conduit::id::nodeState).toString() == "Deleting");
    CHECK (rig.nodes().getNumChildren() == 2);
    CHECK (rig.graph.getNumNodes() == 2);

    // Phase 2 im nächsten Loop-Durchlauf: Subtree + Kabel weg, Graph synchron
    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.nodes().getNumChildren() == 1);
    CHECK (rig.connections().getNumChildren() == 0);
    CHECK (rig.graph.getNumNodes() == 1);
    CHECK (rig.graph.getConnections().size() == 0u);

    // Undo stellt Node + Kabel in einem Schritt wieder her,
    // nodeState wird beim Re-Materialisieren auf Active zurückgesetzt
    REQUIRE (rig.undoManager.undo());
    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.nodes().getNumChildren() == 2);
    CHECK (rig.graph.getNumNodes() == 2);
    CHECK (rig.graph.getConnections().size() == 1u);

    const auto restored = rig.nodes().getChildWithProperty (conduit::id::nodeId, sourceUuid);
    REQUIRE (restored.isValid());
    CHECK (restored.getProperty (conduit::id::nodeState).toString() == "Active");
}

//==============================================================================
TEST_CASE ("Zweiphasiges Delete: UI-Referenz blockiert Phase 2 (Zombie-UI-Schutz)",
           "[GraphManager]")
{
    TestRig rig;

    auto node = rig.addModuleNode (attenuatorId);
    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.graph.getNumNodes() == 1);

    const auto nodeUuid = TestRig::uuidOf (node);

    // Eine UI-Component ist an den Subtree gebunden
    rig.uiRegistry.acquire (nodeUuid);

    REQUIRE (rig.manager.requestNodeDelete (nodeUuid));
    rig.manager.flushPendingTopologyUpdate();

    // Phase 2 wartet: Subtree und Graph-Node bleiben bestehen
    CHECK (rig.nodes().getNumChildren() == 1);
    CHECK (rig.graph.getNumNodes() == 1);

    // Die letzte UI-Component koppelt ab → Registry-Callback stößt Phase 2 an
    rig.uiRegistry.release (nodeUuid);
    rig.manager.flushPendingTopologyUpdate();
    CHECK (rig.nodes().getNumChildren() == 0);
    CHECK (rig.graph.getNumNodes() == 0);
}

//==============================================================================
TEST_CASE ("requestNodeDelete: unbekannte nodeId wird abgelehnt", "[GraphManager]")
{
    TestRig rig;

    CHECK_FALSE (rig.manager.requestNodeDelete ("nicht-vorhanden"));
}

//==============================================================================
// MIDI-Rig M5c: Macro-Modulation (ParamModulationBus)

TEST_CASE ("Macro-Modulation: Atomic traegt den Effektivwert, Tree behaelt die Basis",
           "[GraphManager][midirig][macromod]")
{
    TestRig rig;
    // Ueber die Patch-Aktion (createState erzeugt den Parameters-Subtree).
    auto node = rig.manager.addModuleNode (attenuatorId, {});
    rig.manager.flushPendingTopologyUpdate();

    const auto uuid = TestRig::uuidOf (node);
    auto param = node.getChildWithName (conduit::id::parameters)
                     .getChildWithProperty (conduit::id::paramId, "gain");
    REQUIRE (param.isValid());

    auto* module = rig.manager.getModuleFor (uuid);
    REQUIRE (module != nullptr);
    auto* atomicTarget = module->getParameterTarget ("gain");
    REQUIRE (atomicTarget != nullptr);

    // Basis setzen (Listener spiegelt ins Atomic).
    param.setProperty (conduit::id::paramValue, 0.25, nullptr);
    CHECK (atomicTarget->load (std::memory_order_relaxed) == Catch::Approx (0.25f));

    // Modulation +0.5 ueber die Range 0..1 -> effektiv 0.75; Tree unangetastet.
    rig.manager.setParamModulation ({ uuid, "gain" }, 0.5f);
    CHECK (atomicTarget->load (std::memory_order_relaxed) == Catch::Approx (0.75f));
    CHECK ((double) param.getProperty (conduit::id::paramValue) == Catch::Approx (0.25));

    // Basis-Drag komponiert weiter (syncParameterValue-Hook).
    param.setProperty (conduit::id::paramValue, 0.4, nullptr);
    CHECK (atomicTarget->load (std::memory_order_relaxed) == Catch::Approx (0.9f));

    // Effektivwert-Anzeige rechnet OHNE Modul-Zugriff.
    const auto effective = rig.manager.getParamModulationEffective (uuid, "gain");
    REQUIRE (effective.has_value());
    CHECK (*effective == Catch::Approx (0.9f));

    // Clamp am oberen Rand.
    rig.manager.setParamModulation ({ uuid, "gain" }, 1.0f);
    CHECK (atomicTarget->load (std::memory_order_relaxed) == Catch::Approx (1.0f));

    // Clear -> Basis kehrt zurueck, Anzeige leer.
    rig.manager.clearParamModulation ({ uuid, "gain" });
    CHECK (atomicTarget->load (std::memory_order_relaxed) == Catch::Approx (0.4f));
    CHECK_FALSE (rig.manager.getParamModulationEffective (uuid, "gain").has_value());
}

TEST_CASE ("Macro-Modulation: ueberlebt Node-Delete + Re-Add (Uuid-keyed, Re-Apply beim Rebuild)",
           "[GraphManager][midirig][macromod]")
{
    TestRig rig;
    auto node = rig.manager.addModuleNode (attenuatorId, {});
    rig.manager.flushPendingTopologyUpdate();

    const auto uuid = TestRig::uuidOf (node);
    auto param = node.getChildWithName (conduit::id::parameters)
                     .getChildWithProperty (conduit::id::paramId, "gain");
    REQUIRE (param.isValid());
    param.setProperty (conduit::id::paramValue, 0.2, nullptr);

    rig.manager.setParamModulation ({ uuid, "gain" }, 0.5f);

    // Node entfernen: kein Crash, Offset bleibt gemerkt, Anzeige leer.
    rig.nodes().removeChild (node, nullptr);
    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.manager.getModuleFor (uuid) == nullptr);

    rig.manager.setParamModulation ({ uuid, "gain" }, 0.5f);   // No-op-Store, kein Crash
    CHECK_FALSE (rig.manager.getParamModulationEffective (uuid, "gain").has_value());

    // Re-Add desselben Subtrees (Undo-Analogie): addNewNodes spiegelt via
    // syncParameterValue -> die gemerkte Modulation greift automatisch wieder.
    rig.nodes().appendChild (node, nullptr);
    rig.manager.flushPendingTopologyUpdate();

    auto* module = rig.manager.getModuleFor (uuid);
    REQUIRE (module != nullptr);
    auto* atomicTarget = module->getParameterTarget ("gain");
    REQUIRE (atomicTarget != nullptr);
    CHECK (atomicTarget->load (std::memory_order_relaxed) == Catch::Approx (0.7f));
}
