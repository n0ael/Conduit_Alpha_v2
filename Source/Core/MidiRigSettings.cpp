#include "MidiRigSettings.h"

namespace conduit
{

namespace
{
    const juce::String keyMidiRigState = "midiRigState";

    const juce::Identifier xmlRoot   ("MidiRigState");
    const juce::Identifier xmlDevice ("Device");

    juce::String kindToKey (RigDeviceKind kind)
    {
        return kind == RigDeviceKind::controller ? "controller" : "soundGenerator";
    }

    RigDeviceKind kindFromKey (const juce::String& key)
    {
        return key == "controller" ? RigDeviceKind::controller : RigDeviceKind::soundGenerator;
    }
}

//==============================================================================
juce::PropertiesFile::Options MidiRigSettings::defaultOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "MidiRig";
    options.filenameSuffix      = ".settings";
    options.folderName          = "Conduit";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

MidiRigSettings::MidiRigSettings (const juce::PropertiesFile::Options& options)
{
    applicationProperties.setStorageParameters (options);
    loadFromFile();
}

MidiRigSettings::~MidiRigSettings()
{
    flush();
}

void MidiRigSettings::flush()
{
    if (auto* file = applicationProperties.getUserSettings())
        file->saveIfNeeded();
}

//==============================================================================
void MidiRigSettings::loadFromFile()
{
    const auto xml = applicationProperties.getUserSettings()->getXmlValue (keyMidiRigState);
    if (xml == nullptr || ! xml->hasTagName (xmlRoot.toString()))
        return;

    migratedFromGridPanel  = xml->getBoolAttribute ("migratedFromGridPanel", false);
    gridControllerDeviceId = juce::Uuid (xml->getStringAttribute ("gridControllerDeviceId"));
    gridOutputDeviceId     = juce::Uuid (xml->getStringAttribute ("gridOutputDeviceId"));

    devices.clear();

    for (const auto* deviceXml : xml->getChildWithTagNameIterator (xmlDevice.toString()))
    {
        RigDevice device;
        device.id          = juce::Uuid (deviceXml->getStringAttribute ("id"));
        device.label       = deviceXml->getStringAttribute ("label");
        device.kind        = kindFromKey (deviceXml->getStringAttribute ("kind"));
        device.midiOutName = deviceXml->getStringAttribute ("midiOutName");
        device.midiInName  = deviceXml->getStringAttribute ("midiInName");

        if (device.id.isNull())
            continue;   // korrupter/handbearbeiteter Eintrag ohne Id — überspringen

        devices.add (device);
    }
}

void MidiRigSettings::writeAndNotify()
{
    juce::XmlElement xml (xmlRoot.toString());

    xml.setAttribute ("migratedFromGridPanel", migratedFromGridPanel);
    xml.setAttribute ("gridControllerDeviceId", gridControllerDeviceId.toDashedString());
    xml.setAttribute ("gridOutputDeviceId", gridOutputDeviceId.toDashedString());

    for (const auto& device : devices)
    {
        auto* deviceXml = xml.createNewChildElement (xmlDevice.toString());
        deviceXml->setAttribute ("id", device.id.toDashedString());
        deviceXml->setAttribute ("label", device.label);
        deviceXml->setAttribute ("kind", kindToKey (device.kind));
        deviceXml->setAttribute ("midiOutName", device.midiOutName);
        deviceXml->setAttribute ("midiInName", device.midiInName);
    }

    applicationProperties.getUserSettings()->setValue (keyMidiRigState, &xml);
    sendChangeMessage();
}

//==============================================================================
RigDevice MidiRigSettings::getDevice (int index) const
{
    return devices[index];
}

int MidiRigSettings::indexOfId (const juce::Uuid& id) const noexcept
{
    for (int i = 0; i < devices.size(); ++i)
        if (devices.getReference (i).id == id)
            return i;

    return -1;
}

juce::Uuid MidiRigSettings::addDevice (const juce::String& label, RigDeviceKind kind)
{
    RigDevice device;
    device.id    = juce::Uuid();
    device.label = label;
    device.kind  = kind;

    devices.add (device);
    writeAndNotify();

    return device.id;
}

void MidiRigSettings::removeDevice (const juce::Uuid& id)
{
    const auto index = indexOfId (id);
    if (index < 0)
        return;

    devices.remove (index);
    writeAndNotify();
}

void MidiRigSettings::setLabel (const juce::Uuid& id, const juce::String& label)
{
    const auto index = indexOfId (id);
    if (index < 0 || devices.getReference (index).label == label)
        return;

    devices.getReference (index).label = label;
    writeAndNotify();
}

void MidiRigSettings::setKind (const juce::Uuid& id, RigDeviceKind kind)
{
    const auto index = indexOfId (id);
    if (index < 0 || devices.getReference (index).kind == kind)
        return;

    devices.getReference (index).kind = kind;
    writeAndNotify();
}

void MidiRigSettings::setMidiOutName (const juce::Uuid& id, const juce::String& portName)
{
    const auto index = indexOfId (id);
    if (index < 0 || devices.getReference (index).midiOutName == portName)
        return;

    devices.getReference (index).midiOutName = portName;
    writeAndNotify();
}

void MidiRigSettings::setMidiInName (const juce::Uuid& id, const juce::String& portName)
{
    const auto index = indexOfId (id);
    if (index < 0 || devices.getReference (index).midiInName == portName)
        return;

    devices.getReference (index).midiInName = portName;
    writeAndNotify();
}

//==============================================================================
void MidiRigSettings::setGridControllerDeviceId (const juce::Uuid& id)
{
    if (gridControllerDeviceId == id)
        return;

    gridControllerDeviceId = id;
    writeAndNotify();
}

void MidiRigSettings::setGridOutputDeviceId (const juce::Uuid& id)
{
    if (gridOutputDeviceId == id)
        return;

    gridOutputDeviceId = id;
    writeAndNotify();
}

void MidiRigSettings::migrateFromGridPanel (const juce::String& controlInName,
                                            const juce::String& gridOutName,
                                            const juce::String& echoInName)
{
    if (migratedFromGridPanel)
        return;

    if (controlInName.isEmpty() && gridOutName.isEmpty() && echoInName.isEmpty())
        return;   // nichts zu migrieren — Flag bleibt frei für einen späteren Lauf

    if (controlInName.isNotEmpty())
    {
        RigDevice controller;
        controller.id         = juce::Uuid();
        controller.label      = "Controller";
        controller.kind       = RigDeviceKind::controller;
        controller.midiInName = controlInName;
        devices.add (controller);
        gridControllerDeviceId = controller.id;
    }

    if (gridOutName.isNotEmpty() || echoInName.isNotEmpty())
    {
        RigDevice gridOut;
        gridOut.id          = juce::Uuid();
        gridOut.label       = "Grid-Ausgang";
        gridOut.kind        = RigDeviceKind::soundGenerator;
        gridOut.midiOutName = gridOutName;
        gridOut.midiInName  = echoInName;   // Noten-Echo (Block H4)
        devices.add (gridOut);
        gridOutputDeviceId = gridOut.id;
    }

    migratedFromGridPanel = true;
    writeAndNotify();
}

} // namespace conduit
