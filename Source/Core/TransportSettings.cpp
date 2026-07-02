#include "TransportSettings.h"

namespace conduit
{

namespace
{
    constexpr const char* startStopSyncKey = "startStopSync";
    constexpr const char* clockOffsetKey   = "clockOffsetMs";
    constexpr const char* automateKey      = "automate";
    constexpr const char* fixedLengthKey   = "fixedLength";
    constexpr const char* tapCountKey      = "tapCount";
}

//==============================================================================
juce::PropertiesFile::Options TransportSettings::defaultOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "Transport";
    options.filenameSuffix      = ".settings";
    options.folderName          = "Conduit";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

//==============================================================================
TransportSettings::TransportSettings (const juce::PropertiesFile::Options& options)
{
    applicationProperties.setStorageParameters (options);
    loadFromFile();
}

TransportSettings::~TransportSettings()
{
    if (auto* file = applicationProperties.getUserSettings())
        file->saveIfNeeded();
}

//==============================================================================
void TransportSettings::loadFromFile()
{
    if (auto* file = applicationProperties.getUserSettings())
    {
        startStopSync = file->getBoolValue (startStopSyncKey, true);
        clockOffsetMs = juce::jlimit (-maxClockOffsetMs, maxClockOffsetMs,
                                      file->getDoubleValue (clockOffsetKey, 0.0));
        automate      = file->getBoolValue (automateKey, false);
        fixedLength   = file->getBoolValue (fixedLengthKey, false);
        tapCount      = juce::jlimit (2, 8, file->getIntValue (tapCountKey, 4));
    }
}

void TransportSettings::writeValue (const char* key, const juce::var& value)
{
    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (key, value);
        file->saveIfNeeded();
    }

    sendChangeMessage();
}

//==============================================================================
void TransportSettings::setStartStopSyncEnabled (bool enabled)
{
    if (startStopSync == enabled)
        return;

    startStopSync = enabled;
    writeValue (startStopSyncKey, enabled);
}

void TransportSettings::setClockOffsetMs (double offsetMs)
{
    const auto clamped = juce::jlimit (-maxClockOffsetMs, maxClockOffsetMs, offsetMs);

    if (juce::exactlyEqual (clamped, clockOffsetMs))
        return;

    clockOffsetMs = clamped;
    writeValue (clockOffsetKey, clamped);
}

void TransportSettings::setAutomateEnabled (bool enabled)
{
    if (automate == enabled)
        return;

    automate = enabled;
    writeValue (automateKey, enabled);
}

void TransportSettings::setFixedLengthEnabled (bool enabled)
{
    if (fixedLength == enabled)
        return;

    fixedLength = enabled;
    writeValue (fixedLengthKey, enabled);
}

void TransportSettings::setTapCount (int taps)
{
    const auto clamped = juce::jlimit (2, 8, taps);

    if (tapCount == clamped)
        return;

    tapCount = clamped;
    writeValue (tapCountKey, clamped);
}

} // namespace conduit
