#pragma once

#include <functional>
#include <vector>

#include <juce_data_structures/juce_data_structures.h>

#include "BrowserContextProvider.h"
#include "BrowserSearchIndex.h"
#include "Modules/ModuleFactory.h"

namespace conduit
{

//==============================================================================
/**
    Headless-Modell des Browser-Panels: Navigationszustand + sichtbare
    Zeilen. Die UI (BrowserPanel) rendert rows() 1:1 und meldet Klicks
    zurück — Navigations-Klicks verarbeitet das Modell selbst, Aktions-
    Zeilen (Modul anlegen, Datei laden) behandelt der Besitzer über die
    Panel-Hooks.

    Zustand lebt in einem EIGENEN ValueTree ("ui.browser") — bewusst NICHT
    am Patch-Root (User-Entscheidung 04.07.2026): Navigation/Suche landen
    nie in Presets und nie in der Undo-Historie (alle setProperty mit
    nullptr-UndoManager).

    Informationsarchitektur (fix, maximal zwei Navigationsebenen):

      Übersicht ─ PROJEKTE ── flache Liste (M6)
                ─ AUDIO ──── Loops / One-Shots / Captures ── flache Liste (M6)
                ─ MODULE ─── [CV/Control- und AudioFX-Header mit ein-
                              gerückten Kategorien] ── flache Modulliste

    MODULE-Kategorien kommen aus den ModuleDescriptors (Kategorie taucht
    automatisch auf, sobald ein Modul sie trägt); Reihenfolge = kanonische
    Liste, Unbekanntes alphabetisch dahinter. Nur Message Thread.
*/
class BrowserModel final
{
public:
    /** UI-agnostische Icon-Referenz — BrowserPanel mappt auf push::Icon
        (Core kennt keine UI-Header). */
    enum class Icon { none, projects, audio, cvControl, audioFx };

    struct Row
    {
        enum class Kind
        {
            section,    // PROJEKTE / AUDIO / MODULE          (navigiert)
            branch,     // CV/Control / AudioFX — Abschnitts-Header (nicht klickbar)
            category,   // z.B. "Reverb/Delay" oder "Loops"    (navigiert)
            module,     // Modul-Eintrag, id = factoryKey      (Aktion, ab M3)
            file,       // Projekt-/Audio-Datei                (Aktion, ab M6)
            action,     // z.B. "Preset laden…"                (Aktion)
            hint        // nicht klickbarer Hinweis/Empty-State
        };

        Kind kind = Kind::hint;
        Icon icon = Icon::none;
        juce::String label;
        juce::String id;      // Section-Name, "branch:Kategorie" oder factoryKey
        int indent = 0;       // eingerückte Ebene (Kategorien unter Ast-Headern)
        juce::String secondary;   // rechtsbündig dim: Kategorie (Suche), Dauer/Format (M6)
    };

    /** worker = geteilter 1-Thread-Pool des Editors (Index-Build M4,
        Verzeichnis-Scans M6); dispatcher = Test-Seam des Suchindex
        (leer = MessageManager::callAsync). */
    BrowserModel (ModuleFactory& factoryToUse, BrowserContextProvider& contextToUse,
                  juce::ThreadPool& workerToUse,
                  BrowserSearchIndex::Dispatcher dispatcherToUse = {});

    //==========================================================================
    // Navigation [Message Thread]

    /** Öffnet den Startbereich der aktiven Page (Panel-Öffnen). */
    void openStartSection();

    void openSection (BrowserContextProvider::Section section);

    /** Eine Ebene hoch (Kategorie → Bereich → Übersicht). */
    void goBack();
    [[nodiscard]] bool canGoBack() const;

    /** Kopfzeile: "Browser" · "MODULE" · "MODULE ▸ AudioFX ▸ Utility" … */
    [[nodiscard]] juce::String breadcrumbText() const;

    /** Klick auf Zeile index: Navigations-Zeilen (section/category)
        verarbeitet das Modell und liefert true; alles andere false —
        der Aufrufer (Panel) behandelt Aktions-Zeilen über seine Hooks. */
    bool activateRow (int rowIndex);

    //==========================================================================
    // Suche (M4) — Debouncing macht das Panel (~120 ms)

    /** Nicht-leerer Text schaltet in den Suchmodus (flache Trefferliste
        über die sichtbaren Bereiche); leerer Text zurück zur Navigation. */
    void setSearchText (const juce::String& text);
    [[nodiscard]] juce::String getSearchText() const;
    [[nodiscard]] bool isSearching() const { return getSearchText().isNotEmpty(); }

    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return visibleRows; }

    /** Feuert nach jedem rows()-Neuaufbau. */
    std::function<void()> onRowsChanged;

    /** Eigener Zustands-Tree — NIE an den Patch-Root hängen. */
    juce::ValueTree state { "ui.browser" };

private:
    void handleContextChanged();
    void rebuildRows();
    void rebuildIndexAsync();

    void buildSearchRows();
    void buildOverviewRows();
    void buildModulesRootRows();
    void buildModuleListRows (const juce::String& branchKey,
                              const juce::String& category);
    void buildAudioRootRows();

    /** Kategorien eines Astes in kanonischer Reihenfolge (Rest alphabetisch). */
    [[nodiscard]] juce::StringArray categoriesFor (ModuleDescriptor::Branch branch) const;

    [[nodiscard]] juce::String currentSectionName() const;
    [[nodiscard]] juce::String currentCategoryId() const;   // "branch:Kategorie" | Audio-Unterbereich
    void setNavigation (const juce::String& sectionName, const juce::String& categoryId);

    ModuleFactory& factory;
    BrowserContextProvider& context;
    BrowserSearchIndex index;

    std::vector<Row> visibleRows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserModel)
};

//==============================================================================
/** Section ↔ String (ValueTree-Property + Row-Ids). */
[[nodiscard]] juce::String toString (BrowserContextProvider::Section section);
[[nodiscard]] BrowserContextProvider::Section sectionFromString (const juce::String& name);

} // namespace conduit
