#include <catch2/catch_test_macros.hpp>

#include "Core/Browser/BrowserModel.h"
#include "DSP/Airwindows/AirwindowsRegistry.h"
#include "TestDispatcher.h"

namespace
{
using Section = conduit::BrowserContextProvider::Section;
using Row     = conduit::BrowserModel::Row;

constexpr int pageMixer  = 1;
constexpr int pageDevice = 3;

/** Factory MIT Registrierung VOR der Model-Konstruktion — der Suchindex
    wird im Model-Ctor gebaut und sähe eine leere Factory sonst nie. */
struct RegisteredFactory
{
    RegisteredFactory() { conduit::registerDefaultModules (factory); }
    conduit::ModuleFactory factory;
};

struct ModelRig
{
    /** Wartet auf den async Index-Build (Dispatcher-Queue pumpen). */
    bool waitForSearchRows (const juce::String& needle)
    {
        model.setSearchText (needle);

        return dispatcher.pumpUntil ([this]
        {
            return ! model.rows().empty()
                   && model.rows().front().kind == Row::Kind::module;
        });
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    RegisteredFactory registered;
    conduit::ModuleFactory& factory = registered.factory;
    conduit::BrowserContextProvider context;
    juce::ThreadPool worker { juce::ThreadPoolOptions{}.withNumberOfThreads (1) };
    conduit::test::QueueDispatcher dispatcher;
    conduit::BrowserModel model { factory, context, worker, dispatcher.fn() };
};

bool hasRowWithLabel (const std::vector<Row>& rows, const juce::String& label)
{
    return std::any_of (rows.begin(), rows.end(),
                        [&label] (const Row& row) { return row.label == label; });
}
} // namespace

//==============================================================================
TEST_CASE ("Browser-Modell: Übersicht zeigt die Bereiche kontextabhängig", "[browser]")
{
    ModelRig rig;

    // Device-Page (Default): alle drei Bereiche
    REQUIRE (rig.model.rows().size() == 3);
    REQUIRE (hasRowWithLabel (rig.model.rows(), "PROJEKTE"));
    REQUIRE (hasRowWithLabel (rig.model.rows(), "AUDIO"));
    REQUIRE (hasRowWithLabel (rig.model.rows(), "MODULE"));

    // andere Page: MODULE verschwindet aus der Übersicht
    rig.context.setActivePage (pageMixer);
    REQUIRE (rig.model.rows().size() == 2);
    REQUIRE_FALSE (hasRowWithLabel (rig.model.rows(), "MODULE"));
}

TEST_CASE ("Browser-Modell: Startbereich + Zurück-Navigation", "[browser]")
{
    ModelRig rig;

    // Öffnen auf der Device-Page → direkt in MODULE (User-Entscheidung)
    rig.model.openStartSection();
    REQUIRE (rig.model.breadcrumbText() == "MODULE");
    REQUIRE (rig.model.canGoBack());

    // MODULE-Wurzel: beide Ast-Header mit eingerückten Kategorien
    REQUIRE (hasRowWithLabel (rig.model.rows(), "CV/Control"));
    REQUIRE (hasRowWithLabel (rig.model.rows(), "AudioFX"));
    REQUIRE (hasRowWithLabel (rig.model.rows(), "Dynamics"));
    REQUIRE (hasRowWithLabel (rig.model.rows(), "Reverb/Delay"));

    // Zurück-Pfeil führt zur Bereichsübersicht
    rig.model.goBack();
    REQUIRE (rig.model.breadcrumbText() == "Browser");
    REQUIRE_FALSE (rig.model.canGoBack());
}

TEST_CASE ("Browser-Modell: maximal zwei Ebenen bis zur flachen Modulliste",
           "[browser]")
{
    ModelRig rig;
    rig.model.openSection (Section::modules);

    // Ast-Header sind reine Beschriftung (nicht klickbar), Kategorien
    // eingerückt darunter (indent 1)
    const auto& rootRows = rig.model.rows();
    REQUIRE (rootRows[0].kind == Row::Kind::branch);
    REQUIRE (rootRows[0].indent == 0);
    REQUIRE (rootRows[1].kind == Row::Kind::category);
    REQUIRE (rootRows[1].indent == 1);
    REQUIRE_FALSE (rig.model.activateRow (0));   // Header konsumiert nicht

    // Kategorie anklicken → flache Modulliste + voller Breadcrumb
    const auto findRow = [&rig] (const juce::String& label)
    {
        const auto& rows = rig.model.rows();
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(size_t) i].label == label)
                return i;
        return -1;
    };

    REQUIRE (rig.model.activateRow (findRow ("Reverb/Delay")));
    REQUIRE (rig.model.breadcrumbText()
                 == juce::String::fromUTF8 ("MODULE \xe2\x96\xb8 AudioFX \xe2\x96\xb8 Reverb/Delay"));

    for (const auto& row : rig.model.rows())
    {
        REQUIRE (row.kind == Row::Kind::module);
        REQUIRE (row.id.startsWith ("airwindows_"));
    }
    REQUIRE (hasRowWithLabel (rig.model.rows(), "Chamber"));
    REQUIRE (hasRowWithLabel (rig.model.rows(), "TapeDelay2"));

    // goBack-Kette: Modulliste → MODULE-Wurzel → Übersicht
    rig.model.goBack();
    REQUIRE (rig.model.breadcrumbText() == "MODULE");
    rig.model.goBack();
    REQUIRE (rig.model.breadcrumbText() == "Browser");
}

TEST_CASE ("Browser-Modell: Kategorien decken alle Module ab (kein Verwaister)",
           "[browser]")
{
    ModelRig rig;
    rig.model.openSection (Section::modules);

    // Alle Kategorien der MODULE-Wurzel ablaufen und Module einsammeln
    juce::StringArray seen;
    const auto rootRows = rig.model.rows();   // Kopie (Navigation ändert rows)

    for (const auto& rootRow : rootRows)
    {
        if (rootRow.kind != Row::Kind::category)
            continue;

        rig.model.openSection (Section::modules);
        const auto& current = rig.model.rows();
        for (int i = 0; i < (int) current.size(); ++i)
        {
            if (current[(size_t) i].id == rootRow.id)
            {
                REQUIRE (rig.model.activateRow (i));
                break;
            }
        }

        for (const auto& moduleRow : rig.model.rows())
        {
            REQUIRE (moduleRow.kind == Row::Kind::module);   // keine leere Kategorie
            seen.addIfNotAlreadyThere (moduleRow.id);
        }
    }

    // Jeder Factory-Eintrag taucht in genau seiner Kategorie auf
    conduit::ModuleFactory factory;
    conduit::registerDefaultModules (factory);
    REQUIRE (seen.size() == (int) factory.getDescriptors().size());
}

TEST_CASE ("Browser-Modell: AUDIO-Unterbereiche als Navigationsebene", "[browser]")
{
    ModelRig rig;
    rig.model.openSection (Section::audio);

    REQUIRE (rig.model.rows().size() == 3);
    REQUIRE (hasRowWithLabel (rig.model.rows(), "Loops"));
    REQUIRE (hasRowWithLabel (rig.model.rows(), "One-Shots"));
    REQUIRE (hasRowWithLabel (rig.model.rows(), "Captures"));

    REQUIRE (rig.model.activateRow (0));   // Loops
    REQUIRE (rig.model.breadcrumbText()
                 == juce::String::fromUTF8 ("AUDIO \xe2\x96\xb8 Loops"));

    rig.model.goBack();
    REQUIRE (rig.model.breadcrumbText() == "AUDIO");
}

TEST_CASE ("Browser-Modell: Klick auf Bereichs-Zeile navigiert", "[browser]")
{
    ModelRig rig;

    // Zeile 0 = PROJEKTE (fixe Reihenfolge)
    REQUIRE (rig.model.activateRow (0));
    REQUIRE (rig.model.breadcrumbText() == "PROJEKTE");

    // Hinweis-Zeile (Inhalte folgen) konsumiert den Klick nicht
    REQUIRE_FALSE (rig.model.activateRow (0));
}

TEST_CASE ("Browser-Modell: Kontextwechsel verlässt unsichtbare Bereiche", "[browser]")
{
    ModelRig rig;

    rig.model.openSection (Section::modules);
    REQUIRE (rig.model.breadcrumbText() == "MODULE");

    // Device-Page verlassen → MODULE unsichtbar → Startbereich der neuen Page
    rig.context.setActivePage (pageMixer);
    REQUIRE (rig.model.breadcrumbText() == "PROJEKTE");

    // PROJEKTE bleibt bei weiteren Wechseln stehen (global sichtbar)
    rig.context.setActivePage (pageDevice);
    REQUIRE (rig.model.breadcrumbText() == "PROJEKTE");
}

//==============================================================================
TEST_CASE ("Browser-Modell: Suche liefert flache Trefferliste mit Kategorie",
           "[browser][search]")
{
    ModelRig rig;

    REQUIRE (rig.waitForSearchRows ("chamber"));

    REQUIRE (rig.model.rows().size() == 1);
    const auto& hit = rig.model.rows().front();
    REQUIRE (hit.kind == Row::Kind::module);
    REQUIRE (hit.id == "airwindows_chamber");
    REQUIRE (hit.secondary == "Reverb/Delay");
    REQUIRE (rig.model.breadcrumbText() == "Suche");
    REQUIRE (rig.model.canGoBack());

    // Suche über Tags: "sibilance" → DeBess
    REQUIRE (rig.waitForSearchRows ("sibilance"));
    REQUIRE (rig.model.rows().front().id == "airwindows_debess");

    // Leerer Text → zurück zur Navigation (Übersicht der Device-Page)
    rig.model.setSearchText ({});
    REQUIRE (rig.model.breadcrumbText() == "Browser");
    REQUIRE (rig.model.rows().size() == 3);
}

TEST_CASE ("Browser-Modell: Suche — Empty-State und Kontext-Filter",
           "[browser][search]")
{
    ModelRig rig;

    // Erst sicherstellen, dass der Index steht (ein Treffer kommt durch)
    REQUIRE (rig.waitForSearchRows ("lfo"));

    // Unsinniger Begriff → sauberer Empty-State
    rig.model.setSearchText ("xyzzy_nichts");
    REQUIRE (rig.model.rows().size() == 1);
    REQUIRE (rig.model.rows().front().kind == Row::Kind::hint);
    REQUIRE (rig.model.rows().front().label == "Keine Treffer");

    // Module sind nur im Device-Kontext sichtbar — außerhalb liefert
    // dieselbe Suche keine Modul-Treffer
    rig.context.setActivePage (pageMixer);
    rig.model.setSearchText ("lfo");
    REQUIRE (rig.model.rows().front().kind == Row::Kind::hint);

    // goBack löscht die Suche (Navigation bleibt stehen)
    rig.model.goBack();
    REQUIRE_FALSE (rig.model.isSearching());
    REQUIRE (rig.model.getSearchText().isEmpty());
}

//==============================================================================
TEST_CASE ("Modul-Descriptors: jeder registrierte Key trägt Branch + Kategorie",
           "[browser][factory]")
{
    conduit::ModuleFactory factory;
    conduit::registerDefaultModules (factory);

    const auto descriptors = factory.getDescriptors();

    // 57 Airwindows + 6 CV/Control-Module
    REQUIRE (descriptors.size() == 63);

    for (const auto& descriptor : descriptors)
    {
        INFO ("Modul " << descriptor.id);
        REQUIRE (descriptor.id.isNotEmpty());
        REQUIRE (descriptor.displayName.isNotEmpty());
        REQUIRE (descriptor.category.isNotEmpty());
        REQUIRE (factory.isRegistered (descriptor.id));
    }

    // Sortierung: displayName aufsteigend (Browser-Anzeige)
    for (size_t i = 1; i < descriptors.size(); ++i)
        REQUIRE (descriptors[i - 1].displayName
                     .compareIgnoreCase (descriptors[i].displayName) <= 0);
}

TEST_CASE ("Airwindows-Registry: Kategorien aus der erlaubten Menge, Tags gesetzt",
           "[browser][factory]")
{
    const juce::StringArray allowed { "Dynamics", "Filter/EQ", "Distortion/Saturation",
                                      "Lo-Fi/Tape", "Modulation", "Console",
                                      "Reverb/Delay", "Utility" };

    int count = 0;
    for (const auto& entry : conduit::airwindows::getRegisteredPlugins())
    {
        INFO ("Plugin " << entry.id);
        REQUIRE (allowed.contains (juce::String (entry.category)));
        REQUIRE (juce::String (entry.tags).isNotEmpty());
        ++count;
    }

    REQUIRE (count == 57);
}

TEST_CASE ("Modul-Descriptors: Airwindows-Wrapper spiegeln die Registry",
           "[browser][factory]")
{
    conduit::ModuleFactory factory;
    conduit::registerDefaultModules (factory);

    for (const auto& entry : conduit::airwindows::getRegisteredPlugins())
    {
        const auto factoryKey = "airwindows_" + juce::String (entry.id);
        const auto descriptor = factory.getDescriptor (factoryKey);

        INFO ("Plugin " << entry.id);
        REQUIRE (descriptor.id == factoryKey);
        REQUIRE (descriptor.displayName == juce::String (entry.name));
        REQUIRE (descriptor.branch == conduit::ModuleDescriptor::Branch::audioFx);
        REQUIRE (descriptor.category == juce::String (entry.category));
        REQUIRE (! descriptor.tags.isEmpty());
    }
}
