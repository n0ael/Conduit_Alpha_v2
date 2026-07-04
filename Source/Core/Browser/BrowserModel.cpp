#include "BrowserModel.h"

namespace conduit
{

namespace
{
    const juce::Identifier propSection ("section");
    const juce::Identifier propCategory ("category");
    const juce::Identifier propSearch ("searchText");

    constexpr auto sectionProjects = "projects";
    constexpr auto sectionAudio    = "audio";
    constexpr auto sectionModules  = "modules";

    constexpr auto branchCv = "cv";
    constexpr auto branchFx = "fx";

    // Kanonische Kategorie-Reihenfolge pro Ast — unbekannte Kategorien
    // (neue Module) sortieren sich alphabetisch dahinter ein
    const juce::StringArray kCvCategoryOrder { "LFO", "Envelope", "Sequencer",
                                               "Clock", "Analyse", "I/O", "Utility" };
    const juce::StringArray kFxCategoryOrder { "Dynamics", "Filter/EQ",
                                               "Distortion/Saturation", "Lo-Fi/Tape",
                                               "Modulation", "Console",
                                               "Reverb/Delay", "Utility" };

    juce::String branchKeyFor (ModuleDescriptor::Branch branch)
    {
        return branch == ModuleDescriptor::Branch::audioFx ? branchFx : branchCv;
    }

    juce::String branchDisplayName (const juce::String& branchKey)
    {
        return branchKey == branchFx ? "AudioFX" : "CV/Control";
    }

    /** "branch:Kategorie" — Kategorien sind nur pro Ast eindeutig
        (Utility existiert in beiden Ästen). */
    juce::String categoryId (const juce::String& branchKey, const juce::String& category)
    {
        return branchKey + ":" + category;
    }
} // namespace

juce::String toString (BrowserContextProvider::Section section)
{
    switch (section)
    {
        case BrowserContextProvider::Section::projects: return sectionProjects;
        case BrowserContextProvider::Section::audio:    return sectionAudio;
        case BrowserContextProvider::Section::modules:  return sectionModules;
    }

    jassertfalse;
    return {};
}

BrowserContextProvider::Section sectionFromString (const juce::String& name)
{
    if (name == sectionAudio)   return BrowserContextProvider::Section::audio;
    if (name == sectionModules) return BrowserContextProvider::Section::modules;
    return BrowserContextProvider::Section::projects;
}

//==============================================================================
BrowserModel::BrowserModel (ModuleFactory& factoryToUse,
                            BrowserContextProvider& contextToUse,
                            juce::ThreadPool& workerToUse,
                            BrowserSearchIndex::Dispatcher dispatcherToUse)
    : factory (factoryToUse), context (contextToUse),
      index (workerToUse, std::move (dispatcherToUse))
{
    context.onContextChanged = [this] { handleContextChanged(); };

    // Treffer laufen ein, sobald der Hintergrund-Build publiziert hat
    index.onIndexReady = [this]
    {
        if (isSearching())
            rebuildRows();
    };

    rebuildIndexAsync();
    rebuildRows();
}

void BrowserModel::rebuildIndexAsync()
{
    std::vector<BrowserSearchIndex::Source> sources;

    for (const auto& descriptor : factory.getDescriptors())
        sources.push_back ({ descriptor.id, descriptor.displayName,
                             descriptor.category,
                             descriptor.tags.joinIntoString (" ") });

    index.rebuildAsync (std::move (sources));
}

//==============================================================================
void BrowserModel::openStartSection()
{
    openSection (context.startSection());
}

void BrowserModel::openSection (BrowserContextProvider::Section section)
{
    if (! context.isSectionVisible (section))
    {
        // Unsichtbarer Bereich (MODULE außerhalb der Device-Page) →
        // Übersicht statt Sackgasse
        setNavigation ({}, {});
        return;
    }

    setNavigation (toString (section), {});
}

void BrowserModel::goBack()
{
    // Zurück aus der Suche = Suche löschen (Navigation bleibt stehen)
    if (isSearching())
    {
        setSearchText ({});
        return;
    }

    if (currentCategoryId().isNotEmpty())
    {
        setNavigation (currentSectionName(), {});
        return;
    }

    if (currentSectionName().isNotEmpty())
        setNavigation ({}, {});
}

bool BrowserModel::canGoBack() const
{
    return isSearching() || currentSectionName().isNotEmpty();
}

juce::String BrowserModel::breadcrumbText() const
{
    if (isSearching())
        return "Suche";

    const auto section = currentSectionName();

    if (section.isEmpty())
        return "Browser";

    juce::String crumb = section == sectionProjects ? "PROJEKTE"
                       : section == sectionAudio    ? "AUDIO"
                                                    : "MODULE";

    const auto category = currentCategoryId();
    if (category.isEmpty())
        return crumb;

    const auto arrow = juce::String::fromUTF8 (" \xe2\x96\xb8 ");   // ▸

    if (section == sectionModules)
    {
        const auto branchKey = category.upToFirstOccurrenceOf (":", false, false);
        const auto name      = category.fromFirstOccurrenceOf (":", false, false);
        return crumb + arrow + branchDisplayName (branchKey) + arrow + name;
    }

    // AUDIO-Unterbereiche
    const auto label = category == "loops"    ? "Loops"
                     : category == "oneshots" ? "One-Shots"
                                              : "Captures";
    return crumb + arrow + label;
}

bool BrowserModel::activateRow (int rowIndex)
{
    if (rowIndex < 0 || rowIndex >= (int) visibleRows.size())
        return false;

    const auto& row = visibleRows[(size_t) rowIndex];

    switch (row.kind)
    {
        case Row::Kind::section:
            openSection (sectionFromString (row.id));
            return true;

        case Row::Kind::category:
            setNavigation (currentSectionName(), row.id);
            return true;

        // Ast-Header sind reine Abschnitts-Beschriftung; Aktions-Zeilen
        // (module/file/action) behandelt der Panel-Besitzer über Hooks
        case Row::Kind::branch:
        case Row::Kind::module:
        case Row::Kind::file:
        case Row::Kind::action:
        case Row::Kind::hint:
            return false;
    }

    jassertfalse;
    return false;
}

//==============================================================================
void BrowserModel::handleContextChanged()
{
    // Aktive Section unsichtbar geworden (Device-Page verlassen, MODULE
    // offen) → Startbereich der neuen Page; sonst Ansicht behalten
    const auto section = currentSectionName();

    if (section.isNotEmpty()
        && ! context.isSectionVisible (sectionFromString (section)))
    {
        openStartSection();
        return;
    }

    rebuildRows();  // Übersicht kann Zeilen dazubekommen/verlieren
}

void BrowserModel::setSearchText (const juce::String& text)
{
    const auto trimmed = text.trim();

    if (getSearchText() == trimmed)
        return;

    state.setProperty (propSearch, trimmed, nullptr);
    rebuildRows();
}

juce::String BrowserModel::getSearchText() const
{
    return state.getProperty (propSearch).toString();
}

void BrowserModel::buildSearchRows()
{
    // Flache Trefferliste über die sichtbaren Bereiche — M4: Module
    // (nur wenn MODULE im Kontext sichtbar ist), M6 ergänzt Dateien
    if (context.isSectionVisible (BrowserContextProvider::Section::modules))
    {
        for (const auto& itemId : index.query (getSearchText()))
        {
            const auto descriptor = factory.getDescriptor (itemId);
            if (descriptor.id.isEmpty())
                continue;

            visibleRows.push_back ({ Row::Kind::module, Icon::none,
                                     descriptor.displayName, descriptor.id, 0,
                                     descriptor.category });
        }
    }

    if (visibleRows.empty())
        visibleRows.push_back ({ Row::Kind::hint, Icon::none,
                                 "Keine Treffer", {}, 0, {} });
}

void BrowserModel::rebuildRows()
{
    visibleRows.clear();

    const auto section  = currentSectionName();
    const auto category = currentCategoryId();

    if (isSearching())
        buildSearchRows();
    else if (section.isEmpty())
        buildOverviewRows();
    else if (section == sectionModules && category.isEmpty())
        buildModulesRootRows();
    else if (section == sectionModules)
        buildModuleListRows (category.upToFirstOccurrenceOf (":", false, false),
                             category.fromFirstOccurrenceOf (":", false, false));
    else if (section == sectionAudio && category.isEmpty())
        buildAudioRootRows();
    else if (section == sectionProjects)
    {
        // Interim bis M6 (Session-Liste): Preset-Load bleibt erreichbar,
        // seit die alte „+"-CallOutBox weg ist (M3)
        visibleRows.push_back ({ Row::Kind::action, Icon::none,
                                 juce::String::fromUTF8 ("Preset laden…"),
                                 "load_preset", 0 });
        visibleRows.push_back ({ Row::Kind::hint, Icon::none,
                                 juce::String::fromUTF8 ("Session-Liste folgt …"), {}, 0 });
    }
    else
        // AUDIO-Unterbereiche: Datenanbindung folgt in M6
        visibleRows.push_back ({ Row::Kind::hint, Icon::none,
                                 juce::String::fromUTF8 ("Inhalte folgen …"), {}, 0 });

    if (onRowsChanged != nullptr)
        onRowsChanged();
}

void BrowserModel::buildOverviewRows()
{
    // Die sichtbaren Hauptbereiche in fixer Reihenfolge
    visibleRows.push_back ({ Row::Kind::section, Icon::projects,
                             "PROJEKTE", sectionProjects, 0 });
    visibleRows.push_back ({ Row::Kind::section, Icon::audio,
                             "AUDIO", sectionAudio, 0 });

    if (context.isSectionVisible (BrowserContextProvider::Section::modules))
        visibleRows.push_back ({ Row::Kind::section, Icon::none,
                                 "MODULE", sectionModules, 0 });
}

void BrowserModel::buildModulesRootRows()
{
    // Beide Äste als Abschnitts-Header, Kategorien eingerückt darunter —
    // „Unterkategorien als eingerückte Ebene, kein tiefer Baum"
    const auto addBranch = [this] (ModuleDescriptor::Branch branch, Icon icon)
    {
        const auto branchKey = branchKeyFor (branch);
        visibleRows.push_back ({ Row::Kind::branch, icon,
                                 branchDisplayName (branchKey), branchKey, 0 });

        for (const auto& category : categoriesFor (branch))
            visibleRows.push_back ({ Row::Kind::category, Icon::none, category,
                                     categoryId (branchKey, category), 1 });
    };

    addBranch (ModuleDescriptor::Branch::cvControl, Icon::cvControl);
    addBranch (ModuleDescriptor::Branch::audioFx, Icon::audioFx);
}

void BrowserModel::buildModuleListRows (const juce::String& branchKey,
                                        const juce::String& category)
{
    const auto branch = branchKey == branchFx ? ModuleDescriptor::Branch::audioFx
                                              : ModuleDescriptor::Branch::cvControl;

    // getDescriptors ist bereits nach displayName sortiert
    for (const auto& descriptor : factory.getDescriptors())
        if (descriptor.branch == branch && descriptor.category == category)
            visibleRows.push_back ({ Row::Kind::module, Icon::none,
                                     descriptor.displayName, descriptor.id, 0 });

    if (visibleRows.empty())
        visibleRows.push_back ({ Row::Kind::hint, Icon::none,
                                 "Keine Module in dieser Kategorie", {}, 0 });
}

void BrowserModel::buildAudioRootRows()
{
    visibleRows.push_back ({ Row::Kind::category, Icon::none, "Loops", "loops", 0 });
    visibleRows.push_back ({ Row::Kind::category, Icon::none, "One-Shots", "oneshots", 0 });
    visibleRows.push_back ({ Row::Kind::category, Icon::none, "Captures", "captures", 0 });
}

juce::StringArray BrowserModel::categoriesFor (ModuleDescriptor::Branch branch) const
{
    juce::StringArray present;

    for (const auto& descriptor : factory.getDescriptors())
        if (descriptor.branch == branch)
            present.addIfNotAlreadyThere (descriptor.category);

    const auto& canonical = branch == ModuleDescriptor::Branch::audioFx
                          ? kFxCategoryOrder : kCvCategoryOrder;

    juce::StringArray ordered;
    for (const auto& category : canonical)
        if (present.contains (category))
            ordered.add (category);

    // Neue/unbekannte Kategorien alphabetisch dahinter
    juce::StringArray rest;
    for (const auto& category : present)
        if (! ordered.contains (category))
            rest.add (category);
    rest.sortNatural();
    ordered.addArray (rest);

    return ordered;
}

//==============================================================================
juce::String BrowserModel::currentSectionName() const
{
    return state.getProperty (propSection).toString();
}

juce::String BrowserModel::currentCategoryId() const
{
    return state.getProperty (propCategory).toString();
}

void BrowserModel::setNavigation (const juce::String& sectionName,
                                  const juce::String& categoryIdToUse)
{
    // nullptr-UndoManager: Browser-Zustand ist nie undo-fähig (Kopf-Doku)
    state.setProperty (propSection, sectionName, nullptr);
    state.setProperty (propCategory, categoryIdToUse, nullptr);
    rebuildRows();
}

} // namespace conduit
