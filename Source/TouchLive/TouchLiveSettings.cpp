#include "TouchLiveSettings.h"

namespace conduit
{

namespace
{
    constexpr const char* hostKey         = "host";
    constexpr const char* commandPortKey  = "commandPort";
    constexpr const char* listenPortKey   = "listenPort";
    constexpr const char* enabledKey      = "enabled";
    constexpr const char* channelWidthKey = "channelWidth";

    [[nodiscard]] bool isValidPort (int port) noexcept
    {
        return port >= 1 && port <= 65535;
    }
}

//==============================================================================
juce::PropertiesFile::Options TouchLiveSettings::defaultOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "TouchLive";
    options.filenameSuffix      = ".settings";
    options.folderName          = "Conduit";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

//==============================================================================
TouchLiveSettings::TouchLiveSettings (const juce::PropertiesFile::Options& options)
{
    applicationProperties.setStorageParameters (options);
    loadFromFile();
}

TouchLiveSettings::~TouchLiveSettings()
{
    if (auto* file = applicationProperties.getUserSettings())
        file->saveIfNeeded();
}

//==============================================================================
void TouchLiveSettings::loadFromFile()
{
    if (auto* file = applicationProperties.getUserSettings())
    {
        host         = file->getValue (hostKey, defaultHost);
        commandPort  = file->getIntValue (commandPortKey, defaultCommandPort);
        listenPort   = file->getIntValue (listenPortKey, defaultListenPort);
        enabled      = file->getBoolValue (enabledKey, false);
        channelWidth = juce::jlimit (minChannelWidth, maxChannelWidth,
                                     file->getIntValue (channelWidthKey, defaultChannelWidth));

        if (! isValidPort (commandPort))
            commandPort = defaultCommandPort;

        if (! isValidPort (listenPort))
            listenPort = defaultListenPort;
    }
}

void TouchLiveSettings::store (const char* key, const juce::var& value)
{
    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (key, value);
        file->saveIfNeeded();
    }
}

void TouchLiveSettings::setHost (const juce::String& newHost)
{
    const auto trimmed = newHost.trim();

    if (trimmed.isEmpty() || trimmed == host)
        return;

    host = trimmed;
    store (hostKey, host);
    sendChangeMessage();
}

void TouchLiveSettings::setCommandPort (int newPort)
{
    if (newPort == commandPort || ! isValidPort (newPort))
        return;

    commandPort = newPort;
    store (commandPortKey, commandPort);
    sendChangeMessage();
}

void TouchLiveSettings::setListenPort (int newPort)
{
    if (newPort == listenPort || ! isValidPort (newPort))
        return;

    listenPort = newPort;
    store (listenPortKey, listenPort);
    sendChangeMessage();
}

void TouchLiveSettings::setEnabled (bool shouldConnect)
{
    if (shouldConnect == enabled)
        return;

    enabled = shouldConnect;
    store (enabledKey, enabled);
    sendChangeMessage();
}

void TouchLiveSettings::setChannelWidth (int newWidth)
{
    const auto clamped = juce::jlimit (minChannelWidth, maxChannelWidth, newWidth);

    if (clamped == channelWidth)
        return;

    channelWidth = clamped;
    store (channelWidthKey, channelWidth);
    sendChangeMessage();
}

} // namespace conduit
