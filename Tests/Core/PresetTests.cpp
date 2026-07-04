#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/EngineProcessor.h"
#include "Modules/AttenuatorModule.h"
#include "TestSettingsFolder.h"

using Catch::Approx;

namespace
{

juce::String uuidOf (const juce::ValueTree& node)
{
    return node.getProperty (conduit::id::nodeId).toString();
}

juce::ValueTree gainParameterOf (const juce::ValueTree& node)
{
    return node.getChildWithName (conduit::id::parameters)
               .getChildWithProperty (conduit::id::paramId, "gain");
}

juce::File tempPresetFile (const juce::String& name)
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile (name + conduit::EngineProcessor::presetFileExtension);
}

constexpr auto attenuatorId = conduit::AttenuatorModule::staticModuleId;

} // namespace

//==============================================================================
TEST_CASE ("Preset-Round-Trip: Patch überlebt Save → frische Engine → Load (5.4)", "[preset]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    const auto file = tempPresetFile ("conduit_roundtrip");

    juce::String savedAttenuatorUuid;

    {
        conduit::EngineProcessor source { settingsFolder.folder };
        auto& manager = source.getGraphManager();

        auto node = manager.addModuleNode (attenuatorId, { 300, 200 });
        savedAttenuatorUuid = uuidOf (node);
        gainParameterOf (node).setProperty (conduit::id::paramValue, 0.3, nullptr);

        const auto ioIn = source.getRootState().getChildWithName (conduit::id::nodes)
                              .getChildWithProperty (conduit::id::factoryId,
                                                     juce::String (conduit::audioInputModuleId));
        REQUIRE (manager.addConnection (uuidOf (ioIn), 0, savedAttenuatorUuid, 0));

        REQUIRE (source.savePreset (file).wasOk());
    }

    conduit::EngineProcessor target { settingsFolder.folder };
    REQUIRE (target.loadPreset (file).wasOk());

    auto nodes = target.getRootState().getChildWithName (conduit::id::nodes);
    const auto restored = nodes.getChildWithProperty (conduit::id::nodeId, savedAttenuatorUuid);
    REQUIRE (restored.isValid());
    REQUIRE ((double) gainParameterOf (restored).getProperty (conduit::id::paramValue)
             == Approx (0.3));

    // I/O-Grundausstattung: aus dem Preset übernommen, NICHT dupliziert
    int ioInputCount = 0;
    for (int i = 0; i < nodes.getNumChildren(); ++i)
        if (nodes.getChild (i).getProperty (conduit::id::factoryId).toString()
            == conduit::audioInputModuleId)
            ++ioInputCount;
    REQUIRE (ioInputCount == 1);

    REQUIRE (target.getRootState().getChildWithName (conduit::id::connections).getNumChildren() == 1);

    // Materialisierung: Modul lebt, Parameter-Initial-Sync trägt den Preset-Wert
    target.getGraphManager().flushPendingTopologyUpdate();
    auto* module = target.getGraphManager().getModuleFor (savedAttenuatorUuid);
    REQUIRE (module != nullptr);
    REQUIRE (juce::exactlyEqual (module->getParameterTarget ("gain")->load (std::memory_order_relaxed),
                                 0.3f));

    file.deleteFile();
}

//==============================================================================
TEST_CASE ("Preset-Load ist undo-fähig — ein Undo stellt den alten Patch wieder her", "[preset]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    const auto file = tempPresetFile ("conduit_undo");

    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };
    auto& manager = engine.getGraphManager();

    // Zustand A: leeres Default-Patch speichern
    REQUIRE (engine.savePreset (file).wasOk());

    // Zustand B: ein Modul hinzufügen
    const auto node = manager.addModuleNode (attenuatorId, {});
    const auto nodeUuid = uuidOf (node);
    const auto countWithModule = engine.getRootState()
                                     .getChildWithName (conduit::id::nodes).getNumChildren();

    // Preset A laden → Modul weg; Undo → Modul wieder da
    REQUIRE (engine.loadPreset (file).wasOk());
    auto nodes = engine.getRootState().getChildWithName (conduit::id::nodes);
    REQUIRE_FALSE (nodes.getChildWithProperty (conduit::id::nodeId, nodeUuid).isValid());

    REQUIRE (engine.getUndoManager().undo());
    nodes = engine.getRootState().getChildWithName (conduit::id::nodes);
    REQUIRE (nodes.getNumChildren() == countWithModule);
    REQUIRE (nodes.getChildWithProperty (conduit::id::nodeId, nodeUuid).isValid());

    file.deleteFile();
}

//==============================================================================
TEST_CASE ("Preset-Save flusht ausstehende OSC-Werte (isDirty-Guard 6.1)", "[preset]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    const auto file = tempPresetFile ("conduit_dirty");

    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };
    auto& manager = engine.getGraphManager();
    auto& osc = engine.getOscController();

    const auto node = manager.addModuleNode (attenuatorId, {});
    manager.flushPendingTopologyUpdate();
    osc.flushPendingUpdates();  // Registry aufbauen

    // OSC-Wert empfangen, aber noch NICHT in den Tree geflusht
    // (addModuleNode vergibt die eindeutige named_id "attenuator_1")
    osc.oscMessageReceived (juce::OSCMessage { "/conduit/utility/attenuator_1/gain", 0.7f });
    REQUIRE (osc.isStateDirty());

    REQUIRE (engine.savePreset (file).wasOk());
    REQUIRE_FALSE (osc.isStateDirty());  // Guard hat geflusht

    // Der gespeicherte Wert ist der OSC-Wert — nichts ging verloren
    const auto xml = juce::XmlDocument::parse (file);
    REQUIRE (xml != nullptr);
    const auto loaded = juce::ValueTree::fromXml (*xml);
    const auto savedNode = loaded.getChildWithName (conduit::id::nodes)
                               .getChildWithProperty (conduit::id::nodeId, uuidOf (node));
    REQUIRE ((double) gainParameterOf (savedNode).getProperty (conduit::id::paramValue)
             == Approx (0.7));

    file.deleteFile();
}

//==============================================================================
TEST_CASE ("loadPreset lehnt kaputte Dateien ab und lässt den Zustand unangetastet", "[preset]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };

    const auto nodeCount = engine.getRootState()
                               .getChildWithName (conduit::id::nodes).getNumChildren();

    SECTION ("Datei existiert nicht")
    {
        REQUIRE (engine.loadPreset (juce::File ("Z:/gibt/es/nicht.conduit")).failed());
    }

    SECTION ("Kein XML")
    {
        const auto file = tempPresetFile ("conduit_kaputt");
        file.replaceWithText ("das ist kein xml");
        REQUIRE (engine.loadPreset (file).failed());
        file.deleteFile();
    }

    SECTION ("XML, aber kein Conduit-Preset")
    {
        const auto file = tempPresetFile ("conduit_fremd");
        file.replaceWithText ("<FremdesFormat attribut=\"x\"/>");
        REQUIRE (engine.loadPreset (file).failed());
        file.deleteFile();
    }

    REQUIRE (engine.getRootState().getChildWithName (conduit::id::nodes).getNumChildren()
             == nodeCount);
}
