#include "BrowserModel.h"

#include "BrowserPaths.h"

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
      index (workerToUse, dispatcherToUse),
      scanner (workerToUse, std::move (dispatcherToUse))
{
    context.onContextChanged = [this] { handleContextChanged(); };

    // Treffer laufen ein, sobald der Hintergrund-Build publiziert hat
    index.onIndexReady = [this]
    {
        if (isSearching())
            rebuildRows();
    };

    scanner.onScanComplete = [this] (const juce::String& scanId,
                                     std::vector<BrowserFileScanner::Entry> entries)
    { handleScanComplete (scanId, std::move (entries)); };

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

    // Gescannte Dateien sind mit-durchsuchbar (M6)
    for (const auto& [itemId, row] : searchableFiles)
        sources.push_back ({ itemId, row.label,
                             itemId.startsWith ("project:") ? "Projekt" : "Audio",
                             {} });

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
    // Flache Trefferliste über die sichtbaren Bereiche: Module (nur im
    // Device-Kontext) + gescannte Projekt-/Audio-Dateien (global)
    const auto modulesVisible
        = context.isSectionVisible (BrowserContextProvider::Section::modules);

    for (const auto& itemId : index.query (getSearchText()))
    {
        if (const auto fileHit = searchableFiles.find (itemId);
            fileHit != searchableFiles.end())
        {
            visibleRows.push_back (fileHit->second);
            continue;
        }

        if (! modulesVisible)
            continue;

        const auto descriptor = factory.getDescriptor (itemId);
        if (descriptor.id.isEmpty())
            continue;

        visibleRows.push_back ({ Row::Kind::module, Icon::none,
                                 descriptor.displayName, descriptor.id, 0,
                                 descriptor.category });
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
    else
        buildFileSectionRows (activeFileSectionKey());

    if (onRowsChanged != nullptr)
        onRowsChanged();
}

void BrowserModel::buildOverviewRows()
{
    // Die sichtbaren Hauptbereiche in fixer Reihenfolge
    visibleRows.push_back ({ Row::Kind::section, Icon::projects,
                             "PROJEKTE", sectionProjects, 0, {} });
    visibleRows.push_back ({ Row::Kind::section, Icon::audio,
                             "AUDIO", sectionAudio, 0, {} });

    if (context.isSectionVisible (BrowserContextProvider::Section::modules))
        visibleRows.push_back ({ Row::Kind::section, Icon::none,
                                 "MODULE", sectionModules, 0, {} });
}

void BrowserModel::buildModulesRootRows()
{
    // Beide Äste als Abschnitts-Header, Kategorien eingerückt darunter —
    // „Unterkategorien als eingerückte Ebene, kein tiefer Baum"
    const auto addBranch = [this] (ModuleDescriptor::Branch branch, Icon icon)
    {
        const auto branchKey = branchKeyFor (branch);
        visibleRows.push_back ({ Row::Kind::branch, icon,
                                 branchDisplayName (branchKey), branchKey, 0, {} });

        for (const auto& category : categoriesFor (branch))
            visibleRows.push_back ({ Row::Kind::category, Icon::none, category,
                                     categoryId (branchKey, category), 1, {} });
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
                                     descriptor.displayName, descriptor.id, 0, {} });

    if (visibleRows.empty())
        visibleRows.push_back ({ Row::Kind::hint, Icon::none,
                                 "Keine Module in dieser Kategorie", {}, 0, {} });
}

void BrowserModel::buildAudioRootRows()
{
    visibleRows.push_back ({ Row::Kind::category, Icon::none, "Loops", "loops", 0, {} });
    visibleRows.push_back ({ Row::Kind::category, Icon::none, "One-Shots", "oneshots", 0, {} });
    visibleRows.push_back ({ Row::Kind::category, Icon::none, "Captures", "captures", 0, {} });
}

void BrowserModel::buildFileSectionRows (const juce::String& sectionKey)
{
    if (sectionKey.isEmpty())
        return;

    // PROJEKTE behält den Datei-Dialog als Weg zu beliebigen Pfaden.
    // M7: Session-Save wanderte aus dem Header hierher (die Save-Kachel
    // der Bar ist jetzt die Clip-Save-Geste der Looper-Page).
    if (sectionKey == "projects")
    {
        visibleRows.push_back ({ Row::Kind::action, Icon::none,
                                 juce::String::fromUTF8 ("Session speichern…"),
                                 "save_preset", 0, {} });
        visibleRows.push_back ({ Row::Kind::action, Icon::none,
                                 juce::String::fromUTF8 ("Preset laden…"),
                                 "load_preset", 0, {} });
    }

    const auto scanned = fileRowsBySection.find (sectionKey);

    if (scanned == fileRowsBySection.end())
    {
        visibleRows.push_back ({ Row::Kind::hint, Icon::none,
                                 juce::String::fromUTF8 ("Scanne …"), {}, 0, {} });
        return;
    }

    if (scanned->second.empty())
    {
        visibleRows.push_back ({ Row::Kind::hint, Icon::none,
                                 sectionKey == "projects" ? "Keine Sessions"
                                                          : "Keine Dateien", {}, 0, {} });
        return;
    }

    for (const auto& row : scanned->second)
        visibleRows.push_back (row);
}

juce::String BrowserModel::activeFileSectionKey() const
{
    if (isSearching())
        return {};

    const auto section = currentSectionName();

    if (section == sectionProjects)
        return "projects";

    if (section == sectionAudio)
        return currentCategoryId();   // "loops" | "oneshots" | "captures" | ""

    return {};
}

void BrowserModel::triggerScan (const juce::String& sectionKey)
{
    if (sectionKey.isEmpty())
        return;

    const auto directories = directoriesProvider != nullptr
                           ? directoriesProvider()
                           : Directories { browser_paths::projectsDirectory(),
                                           browser_paths::loopsDirectory(),
                                           browser_paths::oneShotsDirectory(), {} };

    constexpr auto audioWildcard = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";

    if (sectionKey == "projects")
        scanner.scanAsync (sectionKey, directories.projects, "*.conduit", false);
    else if (sectionKey == "loops")
        scanner.scanAsync (sectionKey, directories.loops, audioWildcard, true);
    else if (sectionKey == "oneshots")
        scanner.scanAsync (sectionKey, directories.oneShots, audioWildcard, true);
    else if (sectionKey == "captures")
        scanner.scanAsync (sectionKey, directories.captures, audioWildcard, true);
}

void BrowserModel::handleScanComplete (const juce::String& sectionKey,
                                       std::vector<BrowserFileScanner::Entry> entries)
{
    const auto isProjects = sectionKey == "projects";
    const auto idPrefix   = isProjects ? "project:" : "audio:";

    std::vector<Row> rows;
    rows.reserve (entries.size());

    for (const auto& entry : entries)
    {
        Row row;
        row.kind  = Row::Kind::file;
        row.label = entry.name;
        row.id    = idPrefix + entry.file.getFullPathName();

        if (isProjects)
            row.secondary = juce::Time (entry.modTimeMs).formatted ("%d.%m.%y");
        else
        {
            const auto totalSeconds = juce::roundToInt (entry.durationSeconds);
            row.secondary = juce::String (totalSeconds / 60) + ":"
                          + juce::String (totalSeconds % 60).paddedLeft ('0', 2);
            if (entry.formatSummary.isNotEmpty())
                row.secondary << juce::String::fromUTF8 (" \xc2\xb7 ")   // ·
                              << entry.formatSummary;
        }

        rows.push_back (std::move (row));
    }

    fileRowsBySection[sectionKey] = std::move (rows);

    // Suchindex kennt die Dateien mit — die Map wird komplett aus den
    // Scan-Ergebnissen neu aufgebaut (verschwundene Dateien fallen raus)
    searchableFiles.clear();
    for (const auto& [key, sectionRows] : fileRowsBySection)
        for (const auto& row : sectionRows)
            searchableFiles[row.id] = row;

    rebuildIndexAsync();

    if (activeFileSectionKey() == sectionKey || isSearching())
        rebuildRows();
}

void BrowserModel::refreshFiles()
{
    triggerScan (activeFileSectionKey());
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

    // Dateibereich betreten → frisch scannen (mtime-Cache macht Rescans
    // billig; bis das Ergebnis einläuft, zeigen die letzten Rows/„Scanne …")
    triggerScan (activeFileSectionKey());
}

} // namespace conduit
