#include "MidiTargetBrowserModel.h"

#include <algorithm>

namespace conduit
{

namespace
{
    const juce::String kGeneralSectionLabel = "Allgemein";

    juce::String formatCcOrNrpnSuffix (const midirig::ProfileParam& param)
    {
        return param.nrpn >= 0 ? " (NRPN " + juce::String (param.nrpn) + ")"
                               : " (CC " + juce::String (param.cc) + ")";
    }

    MidiTargetBrowserModel::Row makeProfileParameterRow (const midirig::DeviceProfile& profile,
                                                          const midirig::ProfileParam& param,
                                                          bool withSectionPrefix)
    {
        MidiTargetBrowserModel::Row row;
        row.kind = MidiTargetBrowserModel::Kind::parameter;
        row.label = (withSectionPrefix && param.section.isNotEmpty() ? param.section + ": " + param.name
                                                                     : param.name)
                    + formatCcOrNrpnSuffix (param);
        row.isCc = param.nrpn < 0;
        row.number = param.nrpn >= 0 ? param.nrpn : param.cc;
        row.minValue = param.minValue;
        row.maxValue = param.maxValue;
        row.displayName = profile.device + ": " + param.name;
        return row;
    }
}

//==============================================================================
MidiTargetBrowserModel::MidiTargetBrowserModel (const grid::HardwareCcDatabase& hardwareDbToUse,
                                                const MidiProfileLibrary& profileLibraryToUse)
    : hardwareDb (hardwareDbToUse), profileLibrary (profileLibraryToUse)
{
}

void MidiTargetBrowserModel::setPresetSources (const HardwarePresetLibrary* presetLibraryToUse,
                                               const MidiRigSettings* rigSettingsToUse)
{
    presetLibrary = presetLibraryToUse;
    rigSettings = rigSettingsToUse;
}

std::vector<MidiTargetBrowserModel::Row> MidiTargetBrowserModel::currentRows() const
{
    if (filterText.isEmpty())
    {
        std::vector<Row> rows;
        for (auto& entry : buildCurrentLevel())
            rows.push_back (std::move (entry.row));
        return rows;
    }

    return filterRows (allParameterRowsUnderCurrentPath(), filterText);
}

void MidiTargetBrowserModel::enter (int rowIndex)
{
    filterText.clear();

    const auto entries = buildCurrentLevel();
    if (rowIndex < 0 || rowIndex >= (int) entries.size())
        return;

    const auto kind = entries[(size_t) rowIndex].row.kind;
    if (kind == Kind::parameter || kind == Kind::preset || kind == Kind::action)
        return;   // nicht navigierbar -- Auswahl/Aktion macht der Aufrufer direkt

    path.push_back (entries[(size_t) rowIndex].entry);
}

bool MidiTargetBrowserModel::goBack()
{
    filterText.clear();

    if (path.empty())
        return false;

    path.pop_back();
    return true;
}

juce::String MidiTargetBrowserModel::breadcrumbText() const
{
    juce::StringArray parts;
    for (const auto& entry : path)
        parts.add (entry.label);

    return parts.joinIntoString (" " + juce::String::fromUTF8 ("\xe2\x96\xb8") + " ");
}

void MidiTargetBrowserModel::setFilter (const juce::String& text)
{
    filterText = text;
}

//==============================================================================
std::vector<MidiTargetBrowserModel::LevelEntry> MidiTargetBrowserModel::buildCurrentLevel() const
{
    std::vector<LevelEntry> entries;

    if (path.empty())
    {
        const auto& legacyDevices = hardwareDb.devices();
        for (int i = 0; i < (int) legacyDevices.size(); ++i)
        {
            LevelEntry e;
            e.row.kind = Kind::device;
            e.row.label = legacyDevices[(size_t) i].name;
            e.entry.kind = Kind::device;
            e.entry.label = e.row.label;
            e.entry.intKey = i;
            e.entry.isLegacyDevice = true;
            entries.push_back (std::move (e));
        }

        juce::StringArray seenManufacturers;
        const auto& profiles = profileLibrary.profiles();
        for (const auto& profile : profiles)
        {
            if (profile.manufacturer.isEmpty() || seenManufacturers.contains (profile.manufacturer))
                continue;
            seenManufacturers.add (profile.manufacturer);

            LevelEntry e;
            e.row.kind = Kind::manufacturer;
            e.row.label = profile.manufacturer;
            e.entry.kind = Kind::manufacturer;
            e.entry.label = e.row.label;
            e.entry.stringKey = profile.manufacturer;
            entries.push_back (std::move (e));
        }

        std::stable_sort (entries.begin(), entries.end(),
                          [] (const LevelEntry& a, const LevelEntry& b)
                          { return a.row.label.compareIgnoreCase (b.row.label) < 0; });

        // M9c: Preset-Zweig OBEN anpinnen (kein Sortier-Kandidat) -- nur
        // wenn Quellen gesetzt sind und mindestens ein Klangerzeuger existiert.
        if (presetsEnabled())
        {
            auto hasSoundGenerator = false;
            for (int i = 0; i < rigSettings->getNumDevices() && ! hasSoundGenerator; ++i)
                hasSoundGenerator = rigSettings->getDevice (i).kind
                                        == RigDeviceKind::soundGenerator;

            if (hasSoundGenerator)
            {
                LevelEntry e;
                e.row.kind = Kind::presetRoot;
                e.row.label = "HW Presets";
                e.entry.kind = Kind::presetRoot;
                e.entry.label = e.row.label;
                entries.insert (entries.begin(), std::move (e));
            }
        }
        return entries;
    }

    const auto& top = path.back();

    if (top.kind == Kind::presetRoot || top.kind == Kind::presetDevice
        || top.kind == Kind::presetBank)
        return buildPresetLevel (top);

    if (top.kind == Kind::manufacturer)
    {
        const auto& profiles = profileLibrary.profiles();
        for (int i = 0; i < (int) profiles.size(); ++i)
        {
            const auto& profile = profiles[(size_t) i];
            if (profile.manufacturer != top.stringKey)
                continue;

            LevelEntry e;
            e.row.kind = Kind::device;
            e.row.label = profile.device;
            e.entry.kind = Kind::device;
            e.entry.label = e.row.label;
            e.entry.intKey = i;
            e.entry.isLegacyDevice = false;
            entries.push_back (std::move (e));
        }

        std::stable_sort (entries.begin(), entries.end(),
                          [] (const LevelEntry& a, const LevelEntry& b)
                          { return a.row.label.compareIgnoreCase (b.row.label) < 0; });
        return entries;
    }

    if (top.kind == Kind::device)
    {
        if (top.isLegacyDevice)
        {
            for (auto& row : parameterRowsForLegacyDevice (top.intKey))
                entries.push_back ({ std::move (row), {} });
            return entries;
        }

        const auto sections = distinctSectionsForProfile (top.intKey);
        if (sections.empty())
        {
            for (auto& row : parameterRowsForProfileSection (top.intKey, {}, true))
                entries.push_back ({ std::move (row), {} });
            return entries;
        }

        bool hasUnsectionedParams = false;
        {
            const auto& profiles = profileLibrary.profiles();
            if (top.intKey >= 0 && top.intKey < (int) profiles.size())
                for (const auto& param : profiles[(size_t) top.intKey].params)
                    if (param.section.isEmpty()) { hasUnsectionedParams = true; break; }
        }

        for (const auto& section : sections)
        {
            LevelEntry e;
            e.row.kind = Kind::section;
            e.row.label = section;
            e.entry.kind = Kind::section;
            e.entry.label = section;
            e.entry.stringKey = section;
            e.entry.intKey = top.intKey;   // Profil-Index vom Eltern-device uebernehmen
            entries.push_back (std::move (e));
        }

        if (hasUnsectionedParams)
        {
            LevelEntry e;
            e.row.kind = Kind::section;
            e.row.label = kGeneralSectionLabel;
            e.entry.kind = Kind::section;
            e.entry.label = kGeneralSectionLabel;
            e.entry.stringKey = {};   // leer = Sentinel fuer den "Allgemein"-Sammeltopf
            e.entry.intKey = top.intKey;
            entries.push_back (std::move (e));
        }
        return entries;
    }

    if (top.kind == Kind::section)
    {
        const auto matchEmptySection = top.stringKey.isEmpty();   // "Allgemein"-Sammeltopf
        for (auto& row : parameterRowsForProfileSection (top.intKey, top.stringKey, matchEmptySection))
            entries.push_back ({ std::move (row), {} });
        return entries;
    }

    return entries;   // kind == parameter kann nie Pfad-Ziel sein
}

//==============================================================================
// M9c: Preset-Zweig (ADR 007) — HW Presets -> Geraet -> Bank -> Preset

std::vector<MidiTargetBrowserModel::LevelEntry>
    MidiTargetBrowserModel::buildPresetLevel (const PathEntry& top) const
{
    std::vector<LevelEntry> entries;
    if (! presetsEnabled())
        return entries;

    if (top.kind == Kind::presetRoot)
    {
        for (int i = 0; i < rigSettings->getNumDevices(); ++i)
        {
            const auto device = rigSettings->getDevice (i);
            if (device.kind != RigDeviceKind::soundGenerator)
                continue;

            LevelEntry e;
            e.row.kind = Kind::presetDevice;
            e.row.label = device.label;
            e.row.deviceId = device.id;
            e.entry.kind = Kind::presetDevice;
            e.entry.label = device.label;
            e.entry.stringKey = device.id.toString();
            entries.push_back (std::move (e));
        }
        return entries;
    }

    if (top.kind == Kind::presetDevice)
    {
        const juce::Uuid deviceId { top.stringKey };

        // Scan-Aktion zuoberst; waehrend eines Scans zeigt die Zeile den
        // Fortschritt (scanStatusFor-Hook, der Picker pollt niederfrequent).
        LevelEntry action;
        action.row.kind = Kind::action;
        const auto status = scanStatusFor != nullptr ? scanStatusFor (deviceId)
                                                     : juce::String();
        action.row.label = status.isNotEmpty()
                               ? status
                               : juce::String::fromUTF8 (presetLibrary->hasPresets (deviceId)
                                                             ? "Presets neu scannen\xe2\x80\xa6"
                                                             : "Presets scannen\xe2\x80\xa6");
        action.row.deviceId = deviceId;
        entries.push_back (std::move (action));

        for (int bank = 0; bank < presetLibrary->bankCount (deviceId); ++bank)
        {
            LevelEntry e;
            e.row.kind = Kind::presetBank;
            e.row.label = "Bank " + juce::String (bank + 1);
            e.row.deviceId = deviceId;
            e.row.bank = bank;
            e.entry.kind = Kind::presetBank;
            e.entry.label = e.row.label;
            e.entry.stringKey = top.stringKey;
            e.entry.intKey = bank;
            entries.push_back (std::move (e));
        }
        return entries;
    }

    // top.kind == Kind::presetBank
    for (auto& row : presetRowsForDeviceBank (juce::Uuid { top.stringKey }, top.intKey))
        entries.push_back ({ std::move (row), {} });
    return entries;
}

std::vector<MidiTargetBrowserModel::Row>
    MidiTargetBrowserModel::presetRowsForDeviceBank (const juce::Uuid& deviceId, int bank) const
{
    std::vector<Row> rows;
    if (! presetsEnabled())
        return rows;

    for (int program = 0; program < presetLibrary->programsPerBank (deviceId); ++program)
    {
        const auto name = presetLibrary->presetName (deviceId, bank, program);

        Row row;
        row.kind = Kind::preset;
        row.label = juce::String (program + 1) + ": "
                    + (name.isNotEmpty() ? name : juce::String ("?"));
        row.displayName = name;
        row.deviceId = deviceId;
        row.bank = bank;
        row.program = program;
        rows.push_back (std::move (row));
    }
    return rows;
}

std::vector<MidiTargetBrowserModel::Row> MidiTargetBrowserModel::allPresetRowsUnderCurrentPath() const
{
    if (path.empty() || ! presetsEnabled())
        return {};

    const auto& top = path.back();

    if (top.kind == Kind::presetBank)
        return presetRowsForDeviceBank (juce::Uuid { top.stringKey }, top.intKey);

    if (top.kind == Kind::presetDevice)
    {
        const juce::Uuid deviceId { top.stringKey };
        std::vector<Row> rows;
        for (int bank = 0; bank < presetLibrary->bankCount (deviceId); ++bank)
        {
            auto bankRows = presetRowsForDeviceBank (deviceId, bank);
            for (auto& row : bankRows)
                row.label = "Bank " + juce::String (bank + 1) + " - " + row.label;
            rows.insert (rows.end(), std::make_move_iterator (bankRows.begin()),
                         std::make_move_iterator (bankRows.end()));
        }
        return rows;
    }

    if (top.kind == Kind::presetRoot)
    {
        std::vector<Row> rows;
        for (int i = 0; i < rigSettings->getNumDevices(); ++i)
        {
            const auto device = rigSettings->getDevice (i);
            if (device.kind != RigDeviceKind::soundGenerator)
                continue;

            for (int bank = 0; bank < presetLibrary->bankCount (device.id); ++bank)
            {
                auto bankRows = presetRowsForDeviceBank (device.id, bank);
                for (auto& row : bankRows)
                    row.label = device.label + " - Bank " + juce::String (bank + 1)
                                + " - " + row.label;
                rows.insert (rows.end(), std::make_move_iterator (bankRows.begin()),
                             std::make_move_iterator (bankRows.end()));
            }
        }
        return rows;
    }

    return {};
}

std::vector<MidiTargetBrowserModel::Row>
    MidiTargetBrowserModel::parameterRowsForLegacyDevice (int deviceIndex) const
{
    std::vector<Row> rows;
    const auto& devices = hardwareDb.devices();
    if (deviceIndex < 0 || deviceIndex >= (int) devices.size())
        return rows;

    const auto& device = devices[(size_t) deviceIndex];
    for (const auto& param : device.params)
    {
        Row row;
        row.kind = Kind::parameter;
        row.label = param.name + " (CC " + juce::String (param.cc) + ")";
        row.isCc = true;
        row.number = param.cc;
        row.displayName = device.name + ": " + param.name;
        rows.push_back (std::move (row));
    }
    return rows;
}

std::vector<MidiTargetBrowserModel::Row>
    MidiTargetBrowserModel::parameterRowsForProfileSection (int profileIndex,
                                                             const juce::String& section,
                                                             bool matchEmptySection) const
{
    std::vector<Row> rows;
    const auto& profiles = profileLibrary.profiles();
    if (profileIndex < 0 || profileIndex >= (int) profiles.size())
        return rows;

    const auto& profile = profiles[(size_t) profileIndex];
    for (const auto& param : profile.params)
    {
        const auto matches = matchEmptySection ? param.section.isEmpty() : param.section == section;
        if (matches)
            rows.push_back (makeProfileParameterRow (profile, param, false));
    }
    return rows;
}

std::vector<juce::String> MidiTargetBrowserModel::distinctSectionsForProfile (int profileIndex) const
{
    std::vector<juce::String> sections;
    const auto& profiles = profileLibrary.profiles();
    if (profileIndex < 0 || profileIndex >= (int) profiles.size())
        return sections;

    for (const auto& param : profiles[(size_t) profileIndex].params)
    {
        if (param.section.isEmpty())
            continue;
        if (std::find (sections.begin(), sections.end(), param.section) == sections.end())
            sections.push_back (param.section);
    }
    return sections;
}

std::vector<MidiTargetBrowserModel::Row> MidiTargetBrowserModel::allParameterRowsForProfile (int profileIndex) const
{
    std::vector<Row> rows;
    const auto& profiles = profileLibrary.profiles();
    if (profileIndex < 0 || profileIndex >= (int) profiles.size())
        return rows;

    const auto& profile = profiles[(size_t) profileIndex];
    for (const auto& param : profile.params)
        rows.push_back (makeProfileParameterRow (profile, param, true));
    return rows;
}

std::vector<MidiTargetBrowserModel::Row>
    MidiTargetBrowserModel::allParameterRowsForManufacturer (const juce::String& manufacturer) const
{
    std::vector<Row> rows;
    const auto& profiles = profileLibrary.profiles();
    for (int i = 0; i < (int) profiles.size(); ++i)
    {
        if (profiles[(size_t) i].manufacturer != manufacturer)
            continue;

        auto deviceRows = allParameterRowsForProfile (i);
        for (auto& row : deviceRows)
            row.label = profiles[(size_t) i].device + " - " + row.label;
        rows.insert (rows.end(), std::make_move_iterator (deviceRows.begin()),
                     std::make_move_iterator (deviceRows.end()));
    }
    return rows;
}

std::vector<MidiTargetBrowserModel::Row> MidiTargetBrowserModel::allParameterRowsUnderCurrentPath() const
{
    if (path.empty())
    {
        std::vector<Row> rows;

        const auto& legacyDevices = hardwareDb.devices();
        for (int i = 0; i < (int) legacyDevices.size(); ++i)
        {
            auto deviceRows = parameterRowsForLegacyDevice (i);
            for (auto& row : deviceRows)
                row.label = legacyDevices[(size_t) i].name + " - " + row.label;
            rows.insert (rows.end(), std::make_move_iterator (deviceRows.begin()),
                         std::make_move_iterator (deviceRows.end()));
        }

        juce::StringArray seenManufacturers;
        const auto& profiles = profileLibrary.profiles();
        for (const auto& profile : profiles)
        {
            if (profile.manufacturer.isEmpty() || seenManufacturers.contains (profile.manufacturer))
                continue;
            seenManufacturers.add (profile.manufacturer);

            auto manufacturerRows = allParameterRowsForManufacturer (profile.manufacturer);
            for (auto& row : manufacturerRows)
                row.label = profile.manufacturer + " - " + row.label;
            rows.insert (rows.end(), std::make_move_iterator (manufacturerRows.begin()),
                         std::make_move_iterator (manufacturerRows.end()));
        }
        return rows;
    }

    const auto& top = path.back();

    // M9c: innerhalb des Preset-Zweigs durchsucht der Filter Preset-Namen.
    if (top.kind == Kind::presetRoot || top.kind == Kind::presetDevice
        || top.kind == Kind::presetBank)
        return allPresetRowsUnderCurrentPath();

    if (top.kind == Kind::manufacturer)
        return allParameterRowsForManufacturer (top.stringKey);

    if (top.kind == Kind::device)
        return top.isLegacyDevice ? parameterRowsForLegacyDevice (top.intKey)
                                  : allParameterRowsForProfile (top.intKey);

    if (top.kind == Kind::section)
        return parameterRowsForProfileSection (top.intKey, top.stringKey, top.stringKey.isEmpty());

    return {};
}

std::vector<MidiTargetBrowserModel::Row> MidiTargetBrowserModel::filterRows (std::vector<Row> rows,
                                                                             const juce::String& filterTextToUse)
{
    std::vector<Row> result;
    for (auto& row : rows)
        if (row.label.containsIgnoreCase (filterTextToUse))
            result.push_back (std::move (row));
    return result;
}

} // namespace conduit
