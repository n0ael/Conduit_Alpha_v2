#include <catch2/catch_test_macros.hpp>

#include "Core/EngineProcessor.h"
#include "Modules/AttenuatorModule.h"
#include "UI/NodeCanvas.h"
#include "TestSettingsFolder.h"

namespace
{

juce::String uuidOf (const juce::ValueTree& node)
{
    return node.getProperty (conduit::id::nodeId).toString();
}

juce::File tempPresetFile (const juce::String& name)
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile (name + conduit::EngineProcessor::presetFileExtension);
}

/** Anzahl der Pages-Container direkt am Root — Invariante: genau EINER. */
int countPagesContainers (const juce::ValueTree& root)
{
    int count = 0;

    for (int i = 0; i < root.getNumChildren(); ++i)
        if (root.getChild (i).hasType (conduit::id::pages))
            ++count;

    return count;
}

/** true, wenn IRGENDEIN Pages-Container am Root eine Seite mit dieser Uuid hat. */
bool anyPageHasUuid (const juce::ValueTree& root, const juce::String& pageUuid)
{
    for (int i = 0; i < root.getNumChildren(); ++i)
    {
        const auto child = root.getChild (i);

        if (child.hasType (conduit::id::pages)
            && child.getChildWithProperty (conduit::id::pageUuid, pageUuid).isValid())
            return true;
    }

    return false;
}

/** 0b-Invarianten-Dump: Pages-Container, Seiten-Uuids, activePage, Node-pageUuids. */
juce::String dumpPageInvariants (const juce::ValueTree& root)
{
    juce::String dump;
    dump << "Pages-Container am Root: " << countPagesContainers (root) << "\n";

    for (int i = 0; i < root.getNumChildren(); ++i)
    {
        const auto child = root.getChild (i);

        if (! child.hasType (conduit::id::pages))
            continue;

        dump << "  Container #" << i << " (" << child.getNumChildren() << " Seiten):\n";

        for (int p = 0; p < child.getNumChildren(); ++p)
            dump << "    pageUuid=" << child.getChild (p).getProperty (conduit::id::pageUuid).toString()
                 << " grid=(" << child.getChild (p).getProperty (conduit::id::pageGridX).toString()
                 << "," << child.getChild (p).getProperty (conduit::id::pageGridY).toString() << ")\n";
    }

    dump << "activePage=" << root.getProperty (conduit::id::activePage).toString()
         << " (existiert: " << (anyPageHasUuid (root, root.getProperty (conduit::id::activePage).toString()) ? "ja" : "NEIN") << ")\n";

    const auto nodes = root.getChildWithName (conduit::id::nodes);

    for (int i = 0; i < nodes.getNumChildren(); ++i)
    {
        const auto node = nodes.getChild (i);
        const auto pageUuid = node.getProperty (conduit::id::pageUuid).toString();
        dump << "  Node " << node.getProperty (conduit::id::moduleId).toString()
             << " pageUuid=" << pageUuid
             << " (Seite existiert: " << (anyPageHasUuid (root, pageUuid) ? "ja" : "NEIN") << ")\n";
    }

    return dump;
}

constexpr auto attenuatorId = conduit::AttenuatorModule::staticModuleId;

} // namespace

//==============================================================================
// BUG-SESSION Phase 1.1 — Repro: Ein NACH Multipage gespeicherter Patch
// (Pages-Block + activePage + Node-pageUuids im XML) muss in einer frischen
// Engine MIT angeschlossenem NodeCanvas (Editor offen, wie im Standalone)
// vollständig sichtbar laden. Der Canvas hängt VOR dem Load am Root-Tree —
// exakt die App-Situation (EngineEditor.cpp:38).
TEST_CASE ("Multipage-Roundtrip: neuer Patch lädt mit sichtbaren Nodes (Canvas offen)",
           "[preset][pages]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    const auto file = tempPresetFile ("conduit_pages_roundtrip");

    juce::String savedPageUuid;
    juce::StringArray savedModuleUuids;
    int savedNodeCount = 0;

    {
        conduit::EngineProcessor source { settingsFolder.folder };
        auto& manager = source.getGraphManager();

        savedModuleUuids.add (uuidOf (manager.addModuleNode (attenuatorId, { 300, 200 })));
        savedModuleUuids.add (uuidOf (manager.addModuleNode (attenuatorId, { 520, 200 })));

        savedPageUuid = source.getPageManager().getActivePageUuid();
        savedNodeCount = source.getRootState().getChildWithName (conduit::id::nodes).getNumChildren();

        REQUIRE (savedPageUuid.isNotEmpty());
        REQUIRE (source.savePreset (file).wasOk());
    }

    // Frische Engine + Canvas MIT PageManager (wie der Editor) — der Canvas
    // lauscht bereits während loadPreset auf dem Root-Tree
    conduit::EngineProcessor target { settingsFolder.folder };
    conduit::NodeCanvas canvas { target.getRootState(), target.getGraphManager(),
                                 target.getNodeUiRegistry(),
                                 nullptr, nullptr, nullptr, nullptr, nullptr,
                                 &target.getPageManager(), nullptr };

    REQUIRE (target.loadPreset (file).wasOk());

    const auto root = target.getRootState();
    WARN ("0b-Dump nach Load:\n" << dumpPageInvariants (root).toStdString());

    // Invariante 1: genau EIN Pages-Container am Root
    CHECK (countPagesContainers (root) == 1);

    // Invariante 2: jede Node-pageUuid zeigt auf eine Seite, die der
    // PageManager auch FINDET (nicht nur irgendein Container am Root)
    const auto nodes = root.getChildWithName (conduit::id::nodes);

    for (int i = 0; i < nodes.getNumChildren(); ++i)
    {
        const auto node = nodes.getChild (i);
        INFO ("Node " << node.getProperty (conduit::id::moduleId).toString().toStdString());
        CHECK (target.getPageManager()
                   .findPageByUuid (node.getProperty (conduit::id::pageUuid).toString())
                   .isValid());
    }

    // Invariante 3: activePage zeigt auf eine existierende Seite (via Manager)
    CHECK (target.getPageManager()
               .findPageByUuid (root.getProperty (conduit::id::activePage).toString())
               .isValid());

    // Sichtbarkeit: die gespeicherte Seite war die einzige — ALLE Nodes des
    // Patches müssen als Components auf der aktiven Seite stehen
    CHECK (canvas.getNumNodeComponents() == savedNodeCount);

    file.deleteFile();
}

//==============================================================================
// BUG-SESSION Phase 1.0a — Crash-Probe: Seiten-Übersicht (5-Finger-Ebene ruft
// togglePageOverview) und Seitenwechsel nach dem Load dürfen nicht crashen.
// Headless-Äquivalent der Touch-Geste (Gesten-Ebene ruft dieselben Pfade).
TEST_CASE ("Multipage-Roundtrip: Übersicht + Seitenwechsel nach Load crashen nicht",
           "[preset][pages]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    const auto file = tempPresetFile ("conduit_pages_switch");

    {
        conduit::EngineProcessor source { settingsFolder.folder };
        source.getGraphManager().addModuleNode (attenuatorId, { 300, 200 });
        REQUIRE (source.savePreset (file).wasOk());
    }

    conduit::EngineProcessor target { settingsFolder.folder };
    conduit::NodeCanvas canvas { target.getRootState(), target.getGraphManager(),
                                 target.getNodeUiRegistry(),
                                 nullptr, nullptr, nullptr, nullptr, nullptr,
                                 &target.getPageManager(), nullptr };
    canvas.setBounds (0, 0, 1280, 800);

    REQUIRE (target.loadPreset (file).wasOk());

    // 5-Finger-Pfad (Übersicht auf/zu) + Seitenwechsel über alle Seiten
    canvas.togglePageOverview();
    REQUIRE (canvas.isPageOverviewVisible());
    canvas.togglePageOverview();

    canvas.navigatePages (1, 0);   // Wisch ins Leere → legt Seite an
    canvas.navigatePages (-1, 0);  // zurück

    SUCCEED ("Übersicht + Seitenwechsel ohne Absturz");
    file.deleteFile();
}

//==============================================================================
// BUG-SESSION Regressionsfall 3 — Selbstheilung: Ein Node mit absichtlich
// verwaister pageUuid (keine existierende Seite) wird beim Load der
// Default-Seite zugeordnet statt unsichtbar zu bleiben.
TEST_CASE ("Multipage-Load: verwaiste pageUuid wird auf die Default-Seite geheilt",
           "[preset][pages]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    const auto file = tempPresetFile ("conduit_pages_orphan");

    juce::String moduleUuid;

    {
        conduit::EngineProcessor source { settingsFolder.folder };
        moduleUuid = uuidOf (source.getGraphManager().addModuleNode (attenuatorId, { 300, 200 }));
        REQUIRE (source.savePreset (file).wasOk());
    }

    // pageUuid des Moduls im XML auf eine Fantasie-Seite verbiegen
    {
        const auto xml = juce::XmlDocument::parse (file);
        REQUIRE (xml != nullptr);
        auto patched = juce::ValueTree::fromXml (*xml);
        auto node = patched.getChildWithName (conduit::id::nodes)
                        .getChildWithProperty (conduit::id::nodeId, moduleUuid);
        REQUIRE (node.isValid());
        node.setProperty (conduit::id::pageUuid, juce::Uuid().toString(), nullptr);
        REQUIRE (patched.createXml()->writeTo (file));
    }

    conduit::EngineProcessor target { settingsFolder.folder };
    conduit::NodeCanvas canvas { target.getRootState(), target.getGraphManager(),
                                 target.getNodeUiRegistry(),
                                 nullptr, nullptr, nullptr, nullptr, nullptr,
                                 &target.getPageManager(), nullptr };

    REQUIRE (target.loadPreset (file).wasOk());

    const auto node = target.getRootState().getChildWithName (conduit::id::nodes)
                          .getChildWithProperty (conduit::id::nodeId, moduleUuid);
    REQUIRE (node.isValid());

    // Geheilt: pageUuid zeigt auf eine existierende Seite — und zwar die aktive
    const auto healedPage = node.getProperty (conduit::id::pageUuid).toString();
    CHECK (target.getPageManager().findPageByUuid (healedPage).isValid());
    CHECK (healedPage == target.getPageManager().getActivePageUuid());

    // Sichtbar: die Component existiert auf der aktiven Seite
    CHECK (canvas.findNodeComponent (moduleUuid) != nullptr);

    file.deleteFile();
}

//==============================================================================
// BUG-SESSION Regressionsfall 4 — Bestandsschutz: Ein Alt-Patch OHNE
// Pages-Block (vor Multipage, rootStateVersion 1) lädt weiterhin vollständig;
// die Migration legt genau EINE Seite an und weist alle Nodes zu.
TEST_CASE ("Multipage-Load: Alt-Patch ohne Pages-Block lädt weiterhin (Migration)",
           "[preset][pages]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    const auto file = tempPresetFile ("conduit_pages_legacy");

    int savedNodeCount = 0;

    {
        conduit::EngineProcessor source { settingsFolder.folder };
        source.getGraphManager().addModuleNode (attenuatorId, { 300, 200 });

        // Alt-Zustand herstellen: Pages-Zweig, activePage und alle
        // Node-pageUuids entfernen (Patch von vor der Multipage-Umsetzung);
        // rootStateVersion 3 bleibt — die I/O-Wandlung (ADR 009) war früher.
        auto snapshot = source.getRootState().createCopy();
        snapshot.removeChild (snapshot.getChildWithName (conduit::id::pages), nullptr);
        snapshot.removeProperty (conduit::id::activePage, nullptr);

        auto nodes = snapshot.getChildWithName (conduit::id::nodes);
        savedNodeCount = nodes.getNumChildren();

        for (int i = 0; i < nodes.getNumChildren(); ++i)
            nodes.getChild (i).removeProperty (conduit::id::pageUuid, nullptr);

        REQUIRE (snapshot.createXml()->writeTo (file));
    }

    conduit::EngineProcessor target { settingsFolder.folder };
    conduit::NodeCanvas canvas { target.getRootState(), target.getGraphManager(),
                                 target.getNodeUiRegistry(),
                                 nullptr, nullptr, nullptr, nullptr, nullptr,
                                 &target.getPageManager(), nullptr };

    REQUIRE (target.loadPreset (file).wasOk());

    const auto root = target.getRootState();
    CHECK (countPagesContainers (root) == 1);

    const auto nodes = root.getChildWithName (conduit::id::nodes);

    for (int i = 0; i < nodes.getNumChildren(); ++i)
    {
        INFO ("Node " << nodes.getChild (i).getProperty (conduit::id::moduleId).toString().toStdString());
        CHECK (target.getPageManager()
                   .findPageByUuid (nodes.getChild (i).getProperty (conduit::id::pageUuid).toString())
                   .isValid());
    }

    CHECK (canvas.getNumNodeComponents() == savedNodeCount);

    file.deleteFile();
}
