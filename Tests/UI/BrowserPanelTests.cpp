#include <catch2/catch_test_macros.hpp>

#include "Core/Browser/BrowserModel.h"
#include "UI/Browser/BrowserPanel.h"

namespace
{
/** Factory MIT Registrierung VOR der Model-Konstruktion (Index-Build). */
struct RegisteredFactory
{
    RegisteredFactory() { conduit::registerDefaultModules (factory); }
    conduit::ModuleFactory factory;
};

struct PanelRig
{
    PanelRig()
    {
        panel.setSize (conduit::BrowserPanel::dockWidth, 600);
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    RegisteredFactory registered;
    conduit::ModuleFactory& factory = registered.factory;
    conduit::BrowserContextProvider context;
    juce::ThreadPool worker { juce::ThreadPoolOptions{}.withNumberOfThreads (1) };
    conduit::BrowserModel model { factory, context, worker };
    conduit::BrowserPanel panel { model };
};
} // namespace

//==============================================================================
TEST_CASE ("Browser-Panel: Zeilen spiegeln das Modell, 44-px-Raster", "[browser][ui]")
{
    PanelRig rig;

    REQUIRE (conduit::BrowserPanel::rowHeight >= 44);      // Touch-Target-Regel
    REQUIRE (conduit::BrowserPanel::headerHeight >= 44);
    REQUIRE (rig.panel.getListBox().getRowHeight() >= 44);

    // Übersicht (Device-Default): 3 Bereiche
    REQUIRE (rig.panel.getListBox().getListBoxModel()->getNumRows() == 3);

    rig.model.openSection (conduit::BrowserContextProvider::Section::modules);
    REQUIRE (rig.panel.getListBox().getListBoxModel()->getNumRows()
                 == (int) rig.model.rows().size());
}

TEST_CASE ("Browser-Panel: Öffnen/Schließen steuert die Dock-Breite", "[browser][ui]")
{
    PanelRig rig;

    REQUIRE (rig.panel.currentDockWidth() == 0);
    REQUIRE_FALSE (rig.panel.isOpen());

    int layoutCalls = 0;
    rig.panel.onDockWidthChanged = [&layoutCalls] { ++layoutCalls; };

    // Headless: kein Peer → AnimatedValue springt sofort ans Ziel
    rig.panel.setOpen (true);
    REQUIRE (rig.panel.isOpen());
    REQUIRE (rig.panel.currentDockWidth() == conduit::BrowserPanel::dockWidth);
    REQUIRE (rig.panel.isVisible());
    REQUIRE (layoutCalls > 0);

    rig.panel.setOpen (false);
    REQUIRE (rig.panel.currentDockWidth() == 0);
    REQUIRE_FALSE (rig.panel.isVisible());
}

TEST_CASE ("Browser-Panel: Öffnen navigiert zum Startbereich der Page", "[browser][ui]")
{
    PanelRig rig;

    // Device-Page → direkt MODULE, Zurück-Pfeil sichtbar
    rig.panel.setOpen (true);
    REQUIRE (rig.model.breadcrumbText() == "MODULE");
    REQUIRE (rig.panel.getBackTile().isVisible());

    rig.panel.getBackTile().onClick();
    REQUIRE (rig.model.breadcrumbText() == "Browser");
    REQUIRE_FALSE (rig.panel.getBackTile().isVisible());
}

TEST_CASE ("Browser-Panel: Kategorie-Tap steigt ab, Modul-Tap selektiert",
           "[browser][ui]")
{
    PanelRig rig;
    rig.model.openSection (conduit::BrowserContextProvider::Section::modules);

    // Erste Kategorie unter dem CV/Control-Header antippen (Zeile 1)
    REQUIRE (rig.model.rows()[1].kind == conduit::BrowserModel::Row::Kind::category);
    const auto categoryLabel = rig.model.rows()[1].label;
    rig.panel.activateRowForTest (1);

    REQUIRE (rig.model.breadcrumbText().endsWith (categoryLabel));
    REQUIRE (rig.model.rows().front().kind
                 == conduit::BrowserModel::Row::Kind::module);

    // Modul-Tap: Navigation bleibt stehen, Zeile wird selektiert und der
    // Hook liefert den factoryKey (Editor legt das Modul an)
    juce::String activatedKey;
    rig.panel.onModuleActivated = [&activatedKey] (const juce::String& key,
                                                   juce::Rectangle<int>)
    { activatedKey = key; };

    const auto expectedKey = rig.model.rows().front().id;
    rig.panel.activateRowForTest (0);
    REQUIRE (rig.model.rows().front().kind
                 == conduit::BrowserModel::Row::Kind::module);
    REQUIRE (rig.panel.getListBox().getSelectedRow() == 0);
    REQUIRE (activatedKey == expectedKey);
}

TEST_CASE ("Browser-Panel: PROJEKTE-Interim-Zeile feuert den Aktions-Hook",
           "[browser][ui]")
{
    PanelRig rig;
    rig.model.openSection (conduit::BrowserContextProvider::Section::projects);

    REQUIRE (rig.model.rows().front().kind
                 == conduit::BrowserModel::Row::Kind::action);

    juce::String actionId;
    rig.panel.onAction = [&actionId] (const juce::String& id) { actionId = id; };

    rig.panel.activateRowForTest (0);
    REQUIRE (actionId == "load_preset");

    // Hinweis-Zeile bleibt stumm
    actionId.clear();
    rig.panel.activateRowForTest (1);
    REQUIRE (actionId.isEmpty());
}

TEST_CASE ("Browser-Panel: virtualisierte Zeilen — Komponenten nur für den Viewport",
           "[browser][ui]")
{
    PanelRig rig;
    rig.model.openSection (conduit::BrowserContextProvider::Section::modules);

    // In die größte Kategorie absteigen (Distortion/Saturation, 13 Module)
    const auto& rows = rig.model.rows();
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        if (rows[(size_t) i].label == "Distortion/Saturation")
        {
            rig.panel.activateRowForTest (i);
            break;
        }
    }

    const auto numRows = (int) rig.model.rows().size();
    REQUIRE (numRows >= 13);

    // Panel klein machen: nur ein Ausschnitt passt — die ListBox hält
    // höchstens eine Bildschirmseite an Row-Komponenten (+ Puffer)
    rig.panel.setSize (conduit::BrowserPanel::dockWidth, 300);
    rig.panel.getListBox().updateContent();

    int liveRows = 0;
    for (int i = 0; i < numRows; ++i)
        if (rig.panel.getListBox().getComponentForRowNumber (i) != nullptr)
            ++liveRows;

    REQUIRE (liveRows > 0);
    REQUIRE (liveRows < numRows);   // nie alle Einträge als Komponenten
}
