#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/EngineProcessor.h"
#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "Core/OscController.h"
#include "Modules/AttenuatorModule.h"
#include "Modules/ModuleFactory.h"
#include "TestSettingsFolder.h"

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

constexpr auto attenuatorId = conduit::AttenuatorModule::staticModuleId;

juce::String uuidOf (const juce::ValueTree& node)
{
    return node.getProperty (conduit::id::nodeId).toString();
}

struct NamedIdRig
{
    NamedIdRig()
    {
        conduit::registerDefaultModules (factory);
    }

    void flushAll()
    {
        manager.flushPendingTopologyUpdate();
        osc.flushPendingUpdates();
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    juce::ValueTree root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };
    conduit::SpscQueue<conduit::ParameterUpdate> audioQueue { 64 };
    conduit::OscController osc { root, manager, audioQueue };
};

} // namespace

//==============================================================================
TEST_CASE ("named_ids: eindeutige Defaults — die OSC-Kollision ist weg", "[namedid]")
{
    NamedIdRig rig;

    const auto first  = rig.manager.addModuleNode (attenuatorId, {});
    const auto second = rig.manager.addModuleNode (attenuatorId, {});
    const auto third  = rig.manager.addModuleNode (attenuatorId, {});

    REQUIRE (first.getProperty (conduit::id::moduleId).toString()  == "attenuator_1");
    REQUIRE (second.getProperty (conduit::id::moduleId).toString() == "attenuator_2");
    REQUIRE (third.getProperty (conduit::id::moduleId).toString()  == "attenuator_3");

    // factoryId bleibt der Typ-Schlüssel
    REQUIRE (first.getProperty (conduit::id::factoryId).toString() == attenuatorId);

    rig.flushAll();

    const auto addresses = rig.osc.getRegisteredAddresses();
    REQUIRE (addresses.size() == 3);
    REQUIRE (addresses.contains ("/conduit/utility/attenuator_1/gain"));
    REQUIRE (addresses.contains ("/conduit/utility/attenuator_2/gain"));
    REQUIRE (addresses.contains ("/conduit/utility/attenuator_3/gain"));
}

//==============================================================================
TEST_CASE ("renameNode: OSC-Adresse folgt, Undo stellt sie wieder her", "[namedid]")
{
    NamedIdRig rig;
    const auto node = rig.manager.addModuleNode (attenuatorId, {});
    const auto nodeUuid = uuidOf (node);
    rig.flushAll();

    REQUIRE (rig.manager.renameNode (nodeUuid, "main_vca"));
    rig.osc.flushPendingUpdates();

    auto addresses = rig.osc.getRegisteredAddresses();
    REQUIRE (addresses.contains ("/conduit/utility/main_vca/gain"));
    REQUIRE_FALSE (addresses.contains ("/conduit/utility/attenuator_1/gain"));

    // Messages an die neue Adresse erreichen das Echtzeit-Target
    rig.osc.oscMessageReceived (juce::OSCMessage { "/conduit/utility/main_vca/gain", 0.5f });
    conduit::ParameterUpdate update;
    REQUIRE (rig.audioQueue.pop (update));
    REQUIRE (update.target != nullptr);

    // Undo: alter Name + alte Adresse zurück (named_id ist patchbarer Zustand)
    REQUIRE (rig.undoManager.undo());
    rig.osc.flushPendingUpdates();
    REQUIRE (node.getProperty (conduit::id::moduleId).toString() == "attenuator_1");
    REQUIRE (rig.osc.getRegisteredAddresses().contains ("/conduit/utility/attenuator_1/gain"));
}

//==============================================================================
TEST_CASE ("setNodeColour: Property, No-op, Undo/Redo, Entfernen", "[namedid]")
{
    NamedIdRig rig;
    const auto node = rig.manager.addModuleNode (attenuatorId, {});
    const auto uuid = uuidOf (node);

    // Default: keine Farbe (keine Property)
    REQUIRE_FALSE (node.hasProperty (conduit::id::nodeColour));

    // Setzen
    REQUIRE (rig.manager.setNodeColour (uuid, 0x00ff453au));
    REQUIRE ((juce::uint32) (int) node.getProperty (conduit::id::nodeColour, 0) == 0x00ff453au);

    // No-op bei gleicher Farbe (kein neuer Undo-Schritt)
    REQUIRE (rig.manager.setNodeColour (uuid, 0x00ff453au));

    // Undo → zurück auf keine
    REQUIRE (rig.undoManager.undo());
    REQUIRE_FALSE (node.hasProperty (conduit::id::nodeColour));

    // Redo → Farbe wieder da
    REQUIRE (rig.undoManager.redo());
    REQUIRE ((juce::uint32) (int) node.getProperty (conduit::id::nodeColour, 0) == 0x00ff453au);

    // 0 entfernt die Property (zurück zu keine)
    REQUIRE (rig.manager.setNodeColour (uuid, 0));
    REQUIRE_FALSE (node.hasProperty (conduit::id::nodeColour));

    // Unbekannter Node
    REQUIRE_FALSE (rig.manager.setNodeColour ("unbekannte-uuid", 0x00abcdefu));
}

//==============================================================================
TEST_CASE ("renameNode validiert: Sanitizing, Kollision, Leername", "[namedid]")
{
    NamedIdRig rig;
    const auto first  = rig.manager.addModuleNode (attenuatorId, {});
    const auto second = rig.manager.addModuleNode (attenuatorId, {});

    // Sanitizing: Großschreibung, Leerzeichen, Sonderzeichen
    REQUIRE (rig.manager.renameNode (uuidOf (first), "Mein VCA!"));
    REQUIRE (first.getProperty (conduit::id::moduleId).toString() == "mein_vca");

    // Kollision mit vergebenem Namen → abgelehnt, Tree unverändert
    REQUIRE_FALSE (rig.manager.renameNode (uuidOf (second), "mein_vca"));
    REQUIRE (second.getProperty (conduit::id::moduleId).toString() == "attenuator_2");

    // Nach Sanitizing leer → abgelehnt
    REQUIRE_FALSE (rig.manager.renameNode (uuidOf (second), "!!!"));
    REQUIRE_FALSE (rig.manager.renameNode ("unbekannte-uuid", "egal"));

    // No-op (gleicher Name) ist ok
    REQUIRE (rig.manager.renameNode (uuidOf (first), "mein_vca"));
}

//==============================================================================
TEST_CASE ("Migration: Alt-Bestand ohne factoryId materialisiert weiter", "[namedid]")
{
    NamedIdRig rig;

    // Node alter Bauart: moduleId trägt den Factory-Schlüssel, kein factoryId
    juce::ValueTree legacy (conduit::id::node);
    legacy.setProperty (conduit::id::nodeId, juce::Uuid().toString(), nullptr);
    legacy.setProperty (conduit::id::moduleId, attenuatorId, nullptr);
    rig.root.getChildWithName (conduit::id::nodes).appendChild (legacy, nullptr);

    rig.manager.flushPendingTopologyUpdate();

    REQUIRE (rig.manager.getModuleFor (uuidOf (legacy)) != nullptr);
    REQUIRE (legacy.getProperty (conduit::id::factoryId).toString() == attenuatorId);
}

//==============================================================================
TEST_CASE ("named_id ist persistent: Rename überlebt Preset-Roundtrip (Spec 7)", "[namedid]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile (juce::String ("conduit_namedid")
                                         + conduit::EngineProcessor::presetFileExtension);

    juce::String nodeUuid;

    {
        conduit::EngineProcessor source { settingsFolder.folder };
        const auto node = source.getGraphManager().addModuleNode (attenuatorId, {});
        nodeUuid = uuidOf (node);
        REQUIRE (source.getGraphManager().renameNode (nodeUuid, "neutron_filter"));
        REQUIRE (source.savePreset (file).wasOk());
    }

    conduit::EngineProcessor target { settingsFolder.folder };
    REQUIRE (target.loadPreset (file).wasOk());

    const auto restored = target.getRootState().getChildWithName (conduit::id::nodes)
                              .getChildWithProperty (conduit::id::nodeId, nodeUuid);
    REQUIRE (restored.getProperty (conduit::id::moduleId).toString() == "neutron_filter");
    REQUIRE (restored.getProperty (conduit::id::factoryId).toString() == attenuatorId);

    file.deleteFile();
}

//==============================================================================
TEST_CASE ("I/O-Endpunkte: lesbare Namen, seit ADR 009 regulär löschbar", "[namedid]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };

    const auto nodes = engine.getRootState().getChildWithName (conduit::id::nodes);
    const auto ioIn = nodes.getChildWithProperty (conduit::id::factoryId,
                                                  juce::String (conduit::audioInputModuleId));

    REQUIRE (ioIn.isValid());
    REQUIRE (ioIn.getProperty (conduit::id::moduleId).toString() == "audio_in");

    // ADR 009: keine Reserved-Sperre mehr — I/O ist ein reguläres Modul
    REQUIRE (engine.getGraphManager().requestNodeDelete (uuidOf (ioIn)));
}
