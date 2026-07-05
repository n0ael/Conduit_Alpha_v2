#include "ChannelNames.h"

#include <algorithm>
#include <utility>

namespace conduit
{

namespace
{
    constexpr const char* keyChannelNames = "channelNames";

    // XML-Schema der Persistenz (ein Wert in der PropertiesFile)
    const juce::Identifier xmlRoot      { "ChannelNames" };
    const juce::Identifier xmlDevice    { "Device" };
    const juce::Identifier xmlChannel   { "Channel" };
    const juce::Identifier xmlKey       { "key" };
    const juce::Identifier xmlPrefix    { "prefix" };
    const juce::Identifier xmlDir       { "dir" };
    const juce::Identifier xmlIndex     { "index" };
    const juce::Identifier xmlLabel     { "label" };
    const juce::Identifier xmlImagePath { "imagePath" };
    const juce::Identifier xmlPaired    { "paired" };
    const juce::Identifier xmlLinkSend  { "linkSend" };
    const juce::Identifier xmlColour    { "colour" };

    [[nodiscard]] const char* toString (ChannelNames::Direction direction)
    {
        return direction == ChannelNames::Direction::input ? "in" : "out";
    }
}

//==============================================================================
juce::PropertiesFile::Options ChannelNames::defaultOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "ChannelNames";
    options.filenameSuffix      = ".settings";
    options.folderName          = "Conduit";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

//==============================================================================
ChannelNames::ChannelNames (const juce::PropertiesFile::Options& options)
{
    applicationProperties.setStorageParameters (options);
    loadFromFile();
}

ChannelNames::~ChannelNames()
{
    flush();
}

void ChannelNames::flush()
{
    if (auto* file = applicationProperties.getUserSettings())
        file->saveIfNeeded();
}

//==============================================================================
void ChannelNames::loadFromFile()
{
    devices.clear();

    const auto xml = applicationProperties.getUserSettings()->getXmlValue (keyChannelNames);
    if (xml == nullptr || ! xml->hasTagName (xmlRoot.toString()))
        return;

    for (const auto* deviceXml : xml->getChildWithTagNameIterator (xmlDevice.toString()))
    {
        DeviceEntry device;
        device.deviceKey = deviceXml->getStringAttribute (xmlKey.toString());
        device.prefix    = deviceXml->getStringAttribute (xmlPrefix.toString(),
                                                          stripDeviceSuffix (device.deviceKey));
        if (device.deviceKey.isEmpty())
            continue;  // defensiv — editierte Datei

        for (const auto* channelXml : deviceXml->getChildWithTagNameIterator (xmlChannel.toString()))
        {
            Entry entry;
            entry.direction    = channelXml->getStringAttribute (xmlDir.toString()) == "out"
                               ? Direction::output : Direction::input;
            entry.channelIndex = channelXml->getIntAttribute (xmlIndex.toString(), -1);
            entry.userLabel    = channelXml->getStringAttribute (xmlLabel.toString())
                                     .substring (0, maxLabelLength);
            entry.imagePath       = channelXml->getStringAttribute (xmlImagePath.toString());
            entry.pairedWithNext  = channelXml->getBoolAttribute (xmlPaired.toString(), false);
            entry.linkSendEnabled = channelXml->getBoolAttribute (xmlLinkSend.toString(), false);
            entry.colour          = (juce::uint32) channelXml->getIntAttribute (xmlColour.toString(), 0);

            if (entry.channelIndex >= 0 && ! entry.isEmpty())
                device.entries.push_back (std::move (entry));
        }

        if (! device.entries.empty())
            devices.push_back (std::move (device));
    }
}

void ChannelNames::writeToFile()
{
    juce::XmlElement xml (xmlRoot.toString());

    for (const auto& device : devices)
    {
        auto* deviceXml = xml.createNewChildElement (xmlDevice.toString());
        deviceXml->setAttribute (xmlKey.toString(), device.deviceKey);
        deviceXml->setAttribute (xmlPrefix.toString(), device.prefix);

        for (const auto& entry : device.entries)
        {
            auto* channelXml = deviceXml->createNewChildElement (xmlChannel.toString());
            channelXml->setAttribute (xmlDir.toString(), toString (entry.direction));
            channelXml->setAttribute (xmlIndex.toString(), entry.channelIndex);
            channelXml->setAttribute (xmlLabel.toString(), entry.userLabel);
            channelXml->setAttribute (xmlImagePath.toString(), entry.imagePath);

            if (entry.pairedWithNext)
                channelXml->setAttribute (xmlPaired.toString(), 1);

            if (entry.linkSendEnabled)
                channelXml->setAttribute (xmlLinkSend.toString(), 1);

            if (entry.colour != 0)
                channelXml->setAttribute (xmlColour.toString(), (int) entry.colour);
        }
    }

    applicationProperties.getUserSettings()->setValue (keyChannelNames, &xml);
}

//==============================================================================
std::vector<int> ChannelNames::toChannelList (const juce::BigInteger& mask)
{
    std::vector<int> list;
    for (int bit = 0; bit <= mask.getHighestBit(); ++bit)
        if (mask[bit])
            list.push_back (bit);
    return list;
}

int ChannelNames::toDeviceChannel (Direction direction, int portIndex) const
{
    const auto& map = direction == Direction::input ? activeInputChannelMap
                                                    : activeOutputChannelMap;
    if (portIndex >= 0 && portIndex < (int) map.size())
        return map[(size_t) portIndex];

    return portIndex;  // keine Auswahl-Info (Tests / vor Device-Init) → identisch
}

void ChannelNames::setActiveDevice (const juce::String& deviceName,
                                    const juce::StringArray& reportedInputNames,
                                    const juce::StringArray& reportedOutputNames,
                                    const juce::BigInteger& activeInputChannels,
                                    const juce::BigInteger& activeOutputChannels)
{
    activeDeviceName       = deviceName;
    activeInputNames       = reportedInputNames;
    activeOutputNames      = reportedOutputNames;
    activeInputChannelMap  = toChannelList (activeInputChannels);
    activeOutputChannelMap = toChannelList (activeOutputChannels);
    sendChangeMessage();
}

//==============================================================================
juce::String ChannelNames::getLabel (Direction direction, int channelIndex) const
{
    // channelIndex ist der (komprimierte) Port-Index → echter Geräte-Kanal
    const auto deviceChannel = toDeviceChannel (direction, channelIndex);

    if (const auto* entry = findEntry (direction, deviceChannel);
        entry != nullptr && entry->userLabel.isNotEmpty())
        return entry->userLabel;

    const auto& reported = direction == Direction::input ? activeInputNames
                                                         : activeOutputNames;
    if (deviceChannel >= 0 && deviceChannel < reported.size()
        && reported[deviceChannel].isNotEmpty())
        return reported[deviceChannel];

    return defaultLabel (direction, deviceChannel);
}

juce::String ChannelNames::getUserLabel (Direction direction, int channelIndex) const
{
    if (const auto* entry = findEntry (direction, toDeviceChannel (direction, channelIndex)))
        return entry->userLabel;

    return {};
}

juce::String ChannelNames::getImagePath (Direction direction, int channelIndex) const
{
    if (const auto* entry = findEntry (direction, toDeviceChannel (direction, channelIndex)))
        return entry->imagePath;

    return {};
}

void ChannelNames::setUserLabel (Direction direction, int channelIndex, const juce::String& label)
{
    if (activeDeviceName.isEmpty() || channelIndex < 0)
        return;  // ohne Device-Kontext gibt es keinen Key zum Speichern

    // Am echten Geräte-Kanal verankern (stabil beim Ein-/Ausschalten früherer
    // Kanäle), nicht am komprimierten Port-Index
    const auto deviceChannel = toDeviceChannel (direction, channelIndex);
    const auto trimmed = label.trim().substring (0, maxLabelLength);

    if (trimmed == getUserLabel (direction, channelIndex))
        return;

    auto& device = findOrCreateActiveDevice();
    const auto it = std::find_if (device.entries.begin(), device.entries.end(),
                                  [&] (const Entry& entry)
                                  { return entry.direction == direction
                                        && entry.channelIndex == deviceChannel; });

    if (it != device.entries.end())
        it->userLabel = trimmed;
    else if (trimmed.isNotEmpty())
        device.entries.push_back ({ direction, deviceChannel, trimmed, {} });

    pruneAndStore (device);
}

void ChannelNames::setImagePath (Direction direction, int channelIndex, const juce::String& path)
{
    if (activeDeviceName.isEmpty() || channelIndex < 0)
        return;

    const auto deviceChannel = toDeviceChannel (direction, channelIndex);

    if (path == getImagePath (direction, channelIndex))
        return;

    auto& device = findOrCreateActiveDevice();
    const auto it = std::find_if (device.entries.begin(), device.entries.end(),
                                  [&] (const Entry& entry)
                                  { return entry.direction == direction
                                        && entry.channelIndex == deviceChannel; });

    if (it != device.entries.end())
        it->imagePath = path;
    else if (path.isNotEmpty())
        device.entries.push_back ({ direction, deviceChannel, {}, path });

    pruneAndStore (device);
}

//==============================================================================
juce::uint32 ChannelNames::getColour (Direction direction, int channelIndex) const
{
    if (const auto* entry = findEntry (direction, toDeviceChannel (direction, channelIndex)))
        return entry->colour;

    return 0;
}

void ChannelNames::setColour (Direction direction, int channelIndex, juce::uint32 colour)
{
    if (activeDeviceName.isEmpty() || channelIndex < 0)
        return;  // ohne Device-Kontext gibt es keinen Key zum Speichern

    const auto deviceChannel = toDeviceChannel (direction, channelIndex);

    if (colour == getColour (direction, channelIndex))
        return;

    auto& device = findOrCreateActiveDevice();
    auto& entry  = findOrCreateEntry (device, direction, deviceChannel);
    entry.colour = colour;

    pruneAndStore (device);
}

//==============================================================================
bool ChannelNames::isPortPairStart (Direction direction, int portIndex) const
{
    const auto anchor = toDeviceChannel (direction, portIndex);

    const auto* entry = findEntry (direction, anchor);
    if (entry == nullptr || ! entry->pairedWithNext)
        return false;

    // Paar nur anzeigen, wenn der NÄCHSTE Port physisch auf anchor+1 liegt —
    // bei Teil-Auswahl können Port-Nachbarn auseinanderliegen (das Pairing
    // bleibt gespeichert und greift wieder, sobald beide Kanäle aktiv sind).
    return toDeviceChannel (direction, portIndex + 1) == anchor + 1;
}

void ChannelNames::setPortPairedWithNext (Direction direction, int portIndex, bool paired)
{
    if (activeDeviceName.isEmpty() || portIndex < 0)
        return;  // ohne Device-Kontext gibt es keinen Key zum Speichern

    const auto anchor = toDeviceChannel (direction, portIndex);

    auto& device = findOrCreateActiveDevice();
    auto& entry  = findOrCreateEntry (device, direction, anchor);

    if (entry.pairedWithNext == paired)
    {
        // Auch ein No-op-Set darf keine leere Entry-Leiche hinterlassen
        pruneAndStore (device);
        return;
    }

    entry.pairedWithNext = paired;

    // Konfliktregel: ein Kanal gehört zu höchstens einem Paar — ein neues
    // Paar (anchor, anchor+1) löst Anker auf anchor−1 (überlappt anchor)
    // und anchor+1 (überlappt anchor+1).
    if (paired)
        for (auto& other : device.entries)
            if (other.direction == direction && &other != &entry
                && (other.channelIndex == anchor - 1 || other.channelIndex == anchor + 1))
                other.pairedWithNext = false;

    pruneAndStore (device);
}

bool ChannelNames::isPortLinkSendEnabled (Direction direction, int portIndex) const
{
    const auto* entry = findEntry (direction, toDeviceChannel (direction, portIndex));
    return entry != nullptr && entry->linkSendEnabled;
}

void ChannelNames::setPortLinkSendEnabled (Direction direction, int portIndex, bool enabled)
{
    if (activeDeviceName.isEmpty() || portIndex < 0)
        return;  // ohne Device-Kontext gibt es keinen Key zum Speichern

    const auto deviceChannel = toDeviceChannel (direction, portIndex);

    auto& device = findOrCreateActiveDevice();
    auto& entry  = findOrCreateEntry (device, direction, deviceChannel);

    if (entry.linkSendEnabled != enabled)
        entry.linkSendEnabled = enabled;

    pruneAndStore (device);
}

ChannelNames::Entry& ChannelNames::findOrCreateEntry (DeviceEntry& device, Direction direction,
                                                      int deviceChannel)
{
    const auto it = std::find_if (device.entries.begin(), device.entries.end(),
                                  [&] (const Entry& entry)
                                  { return entry.direction == direction
                                        && entry.channelIndex == deviceChannel; });
    if (it != device.entries.end())
        return *it;

    device.entries.push_back ({ direction, deviceChannel, {}, {}, false });
    return device.entries.back();
}

void ChannelNames::pruneAndStore (DeviceEntry& device)
{
    // Leere Einträge (Label gelöscht, kein Bild, kein Pairing) und leere
    // Devices räumen — die Datei trägt nur echte Overrides
    std::erase_if (device.entries, [] (const Entry& entry) { return entry.isEmpty(); });

    std::erase_if (devices, [] (const DeviceEntry& deviceEntry)
                   { return deviceEntry.entries.empty(); });

    writeToFile();
    sendChangeMessage();
}

//==============================================================================
const ChannelNames::Entry* ChannelNames::findEntry (Direction direction, int channelIndex) const
{
    const auto* device = findMatch (activeDeviceName);
    if (device == nullptr)
        return nullptr;

    const auto it = std::find_if (device->entries.begin(), device->entries.end(),
                                  [&] (const Entry& entry)
                                  { return entry.direction == direction
                                        && entry.channelIndex == channelIndex; });
    return it != device->entries.end() ? &(*it) : nullptr;
}

const ChannelNames::DeviceEntry* ChannelNames::findMatch (const juce::String& deviceName) const
{
    if (deviceName.isEmpty())
        return nullptr;

    // 1. Exakter Name-Match (8.1)
    for (const auto& device : devices)
        if (device.deviceKey == deviceName)
            return &device;

    // 2. Prefix-Match — Suffix wie " (2)" beidseitig ignorieren
    const auto wantedPrefix = stripDeviceSuffix (deviceName);
    for (const auto& device : devices)
        if (device.prefix == wantedPrefix)
            return &device;

    // 3. Kein Match → Defaults
    return nullptr;
}

ChannelNames::DeviceEntry* ChannelNames::findMatch (const juce::String& deviceName)
{
    return const_cast<DeviceEntry*> (std::as_const (*this).findMatch (deviceName));
}

ChannelNames::DeviceEntry& ChannelNames::findOrCreateActiveDevice()
{
    // Schreiben in das gematchte Profil (Prefix-Match aktualisiert das
    // bestehende Profil der Hardware-Familie statt ein zweites anzulegen)
    if (auto* existing = findMatch (activeDeviceName))
        return *existing;

    devices.push_back ({ activeDeviceName, stripDeviceSuffix (activeDeviceName), {} });
    return devices.back();
}

//==============================================================================
juce::String ChannelNames::stripDeviceSuffix (const juce::String& deviceName)
{
    const auto trimmed = deviceName.trim();

    if (! trimmed.endsWithChar (')'))
        return trimmed;

    const auto open = trimmed.lastIndexOf (" (");
    if (open <= 0)
        return trimmed;

    const auto inner = trimmed.substring (open + 2, trimmed.length() - 1);
    if (inner.isEmpty() || ! inner.containsOnly ("0123456789"))
        return trimmed;

    return trimmed.substring (0, open).trim();
}

juce::String ChannelNames::defaultLabel (Direction direction, int channelIndex)
{
    return (direction == Direction::input ? "In " : "Out ")
         + juce::String (channelIndex + 1);
}

juce::String ChannelNames::sanitizeFileLabel (const juce::String& label,
                                              const juce::String& fallback)
{
    juce::String result;
    result.preallocateBytes (static_cast<size_t> (label.length()));

    for (const auto character : label)
        result += (character < 32 || juce::String ("\\/:*?\"<>|").containsChar (character))
                ? juce::juce_wchar ('_')
                : character;

    result = result.trim().substring (0, maxLabelLength);
    return result.isNotEmpty() ? result : fallback;
}

} // namespace conduit
