#include <catch2/catch_test_macros.hpp>

#include "Core/EngineProcessor.h"
#include "Core/PageManager.h"
#include "Modules/AttenuatorModule.h"
#include "TestSettingsFolder.h"

namespace
{

using namespace conduit;

juce::File tempPresetFile (const juce::String& name)
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile (name + EngineProcessor::presetFileExtension);
}

/** Bestandspatch VOR ADR 008: Root ohne Pages-Zweig, ohne rootStateVersion,
    Nodes ohne pageUuid — ein Reserved-I/O-Endpunkt + ein Modul-Node. */
juce::ValueTree makeLegacyRoot()
{
    juce::ValueTree root (id::root);
    juce::ValueTree nodes (id::nodes);

    const auto makeNode = [] (const char* factoryKey, const char* name)
    {
        juce::ValueTree node (id::node);
        node.setProperty (id::nodeId,            juce::Uuid().toString(),      nullptr);
        node.setProperty (id::type,              toString (ModuleType::io),    nullptr);
        node.setProperty (id::factoryId,         factoryKey,                   nullptr);
        node.setProperty (id::moduleId,          name,                         nullptr);
        node.setProperty (id::stateVersion,      1,                            nullptr);
        node.setProperty (id::nodeState,         toString (NodeState::active), nullptr);
        node.setProperty (id::numInputChannels,  0,                            nullptr);
        node.setProperty (id::numOutputChannels, 2,                            nullptr);
        node.appendChild (juce::ValueTree (id::parameters), nullptr);
        return node;
    };

    nodes.appendChild (makeNode (audioInputModuleId, "audio_in"), nullptr);
    nodes.appendChild (makeNode ("attenuator", "legacy_att"), nullptr);

    root.appendChild (nodes, nullptr);
    root.appendChild (juce::ValueTree (id::connections), nullptr);
    root.appendChild (juce::ValueTree (id::calibrationProfiles), nullptr);
    return root;
}

juce::File writeAsPreset (const juce::ValueTree& tree, const juce::String& name)
{
    const auto file = tempPresetFile (name);
    const auto xml = tree.createXml();
    REQUIRE (xml != nullptr);
    REQUIRE (xml->writeTo (file));
    return file;
}

int countPages (const juce::ValueTree& root)
{
    return root.getChildWithName (id::pages).getNumChildren();
}

/** Zählt Struktur-Ereignisse am Tree (rekursiv) — Schutz des Delete-Pfads:
    setNodePage darf NIE als ChildRemoved/ChildAdded erscheinen (ADR 008). */
struct TreeSpy final : juce::ValueTree::Listener
{
    explicit TreeSpy (juce::ValueTree treeToWatch) : tree (std::move (treeToWatch))
    {
        tree.addListener (this);
    }

    ~TreeSpy() override { tree.removeListener (this); }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { ++propertyChanges; }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { ++childAdds; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { ++childRemoves; }

    juce::ValueTree tree;
    int propertyChanges = 0, childAdds = 0, childRemoves = 0;
};

} // namespace

//==============================================================================
TEST_CASE ("Pages-Migration: Legacy-Patch erhält genau eine Seite (0,0) (ADR 008 M1)", "[pages]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;

    const auto file = writeAsPreset (makeLegacyRoot(), "conduit_pages_legacy");

    EngineProcessor engine { settingsFolder.folder };
    REQUIRE (engine.loadPreset (file).wasOk());

    const auto root = engine.getRootState();
    REQUIRE (countPages (root) == 1);

    const auto page = root.getChildWithName (id::pages).getChild (0);
    CHECK ((int) page.getProperty (id::pageGridX) == 0);
    CHECK ((int) page.getProperty (id::pageGridY) == 0);
    const auto defaultUuid = page.getProperty (id::pageUuid).toString();
    REQUIRE (defaultUuid.isNotEmpty());

    // ALLE Nodes tragen die pageUuid der Default-Seite — auch die Reserved-
    // I/O-Endpunkte und das von ensureIONodeStates ergänzte audio_out
    const auto nodes = root.getChildWithName (id::nodes);
    REQUIRE (nodes.getNumChildren() >= 3);

    for (int i = 0; i < nodes.getNumChildren(); ++i)
        CHECK (nodes.getChild (i).getProperty (id::pageUuid).toString() == defaultUuid);

    CHECK ((int) root.getProperty (id::rootStateVersion) == PageManager::pagesRootVersion);
}

TEST_CASE ("Pages-Round-Trip: migrierter Patch lädt ohne erneute Migration (Idempotenz)", "[pages]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    const auto file = tempPresetFile ("conduit_pages_roundtrip");

    juce::String savedPageUuid;

    {
        EngineProcessor source { settingsFolder.folder };
        source.getGraphManager().addModuleNode (AttenuatorModule::staticModuleId, { 100, 100 });

        savedPageUuid = source.getRootState().getChildWithName (id::pages)
                            .getChild (0).getProperty (id::pageUuid).toString();
        REQUIRE (savedPageUuid.isNotEmpty());
        REQUIRE (source.savePreset (file).wasOk());
    }

    EngineProcessor target { settingsFolder.folder };
    REQUIRE (target.loadPreset (file).wasOk());

    const auto root = target.getRootState();

    // Keine zweite Seite, gleiche pageUuid — die Migration hat NICHT erneut
    // zugeschlagen
    REQUIRE (countPages (root) == 1);
    CHECK (root.getChildWithName (id::pages).getChild (0)
               .getProperty (id::pageUuid).toString() == savedPageUuid);

    const auto nodes = root.getChildWithName (id::nodes);
    for (int i = 0; i < nodes.getNumChildren(); ++i)
        CHECK (nodes.getChild (i).getProperty (id::pageUuid).toString() == savedPageUuid);

    CHECK ((int) root.getProperty (id::rootStateVersion) == PageManager::pagesRootVersion);
}

TEST_CASE ("Regel a: nur leere Seiten sind löschbar", "[pages]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;

    EngineProcessor engine { settingsFolder.folder };
    auto& pages = engine.getPageManager();

    const auto defaultUuid = pages.getActivePageUuid();

    // Default-Seite trägt die I/O-Endpunkte → belegt
    CHECK_FALSE (pages.canDeletePage (defaultUuid));
    CHECK_FALSE (pages.deletePage (defaultUuid));

    // Frisch angelegte Seite ist leer → löschbar
    const auto emptyUuid = pages.createPage (1, 0);
    CHECK (pages.canDeletePage (emptyUuid));

    // Alle Nodes auf die neue Seite → Default-Seite wird löschbar
    const auto nodes = engine.getRootState().getChildWithName (id::nodes);

    for (int i = 0; i < nodes.getNumChildren(); ++i)
        REQUIRE (pages.setNodePage (nodes.getChild (i).getProperty (id::nodeId).toString(),
                                    emptyUuid));

    CHECK_FALSE (pages.canDeletePage (emptyUuid));  // jetzt belegt
    CHECK (pages.canDeletePage (defaultUuid));      // jetzt leer
    CHECK (pages.deletePage (defaultUuid));
    CHECK_FALSE (pages.findPageByUuid (defaultUuid).isValid());

    // Unbekannte Uuid: nie löschbar
    CHECK_FALSE (pages.canDeletePage ("keine-seite"));
}

TEST_CASE ("setNodePage ist ein reines setProperty — kein ChildRemoved/ChildAdded (ADR 008)", "[pages]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;

    EngineProcessor engine { settingsFolder.folder };
    auto& pages = engine.getPageManager();

    const auto nodes = engine.getRootState().getChildWithName (id::nodes);
    const auto nodeUuid = nodes.getChild (0).getProperty (id::nodeId).toString();
    const auto targetPage = pages.createPage (1, 0);

    TreeSpy spy { nodes };
    REQUIRE (pages.setNodePage (nodeUuid, targetPage));

    CHECK (spy.propertyChanges == 1);
    CHECK (spy.childAdds == 0);
    CHECK (spy.childRemoves == 0);

    CHECK (PageManager::pageOf (nodes.getChild (0)) == targetPage);
}

TEST_CASE ("Undo: Seiten-Aktionen einzeln undo-/redo-fähig, Migration ohne Undo-History", "[pages]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;

    EngineProcessor engine { settingsFolder.folder };
    auto& pages = engine.getPageManager();
    auto& undo  = engine.getUndoManager();

    // Ctor-Migration (frischer Tree → Pages-Zweig + pageUuids) ist undo-frei
    REQUIRE_FALSE (undo.canUndo());
    const auto root = engine.getRootState();
    REQUIRE (countPages (root) == 1);

    // createPage
    const auto created = pages.createPage (1, 0);
    REQUIRE (countPages (root) == 2);
    REQUIRE (undo.canUndo());
    REQUIRE (undo.undo());
    CHECK (countPages (root) == 1);
    REQUIRE (undo.redo());
    CHECK (countPages (root) == 2);
    CHECK (pages.findPageByUuid (created).isValid());

    // setNodePage
    const auto nodes = root.getChildWithName (id::nodes);
    const auto nodeUuid = nodes.getChild (0).getProperty (id::nodeId).toString();
    const auto before = PageManager::pageOf (nodes.getChild (0));

    REQUIRE (pages.setNodePage (nodeUuid, created));
    CHECK (PageManager::pageOf (nodes.getChild (0)) == created);
    REQUIRE (undo.undo());
    CHECK (PageManager::pageOf (nodes.getChild (0)) == before);
    REQUIRE (undo.redo());
    CHECK (PageManager::pageOf (nodes.getChild (0)) == created);
    REQUIRE (undo.undo());  // Node zurück — Seite (1,0) wieder leer

    // deletePage
    REQUIRE (pages.deletePage (created));
    CHECK (countPages (root) == 1);
    REQUIRE (undo.undo());
    CHECK (countPages (root) == 2);
    CHECK (pages.findPageByUuid (created).isValid());
}
