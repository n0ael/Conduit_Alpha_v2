#include "TransportSettings.h"

namespace conduit
{

namespace
{
    constexpr const char* startStopSyncKey = "startStopSync";
    constexpr const char* clockOffsetKey   = "clockOffsetMs";
    constexpr const char* automateKey      = "automate";
    constexpr const char* fixedLengthKey   = "fixedLength";
    constexpr const char* tapCountKey        = "tapCount";
    constexpr const char* tapAutoCommitKey   = "tapAutoCommit";
    constexpr const char* tapResetHoldKey    = "tapResetHold";
    constexpr const char* metronomeKey       = "metronome";
    constexpr const char* metronomeAnchorKey = "metronomeAnchor";
    constexpr const char* looperSourceKey    = "looperSource";
    constexpr const char* looperAnchorKey    = "looperAnchor";
    constexpr const char* looperSpectrumKey  = "looperSpectrum";

    constexpr double minTapResetHold = 0.3;
    constexpr double maxTapResetHold = 3.0;
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
        tapAutoCommit = file->getBoolValue (tapAutoCommitKey, false);
        tapResetHoldSeconds = juce::jlimit (minTapResetHold, maxTapResetHold,
                                            file->getDoubleValue (tapResetHoldKey, 1.0));
        metronome       = file->getBoolValue (metronomeKey, false);
        metronomeAnchor = juce::jlimit (0, 31, file->getIntValue (metronomeAnchorKey, 0));

        looperSource = file->getValue (looperSourceKey, "master");
        if (looperSource.isEmpty())
            looperSource = "master";
        looperAnchor = juce::jlimit (0, 31, file->getIntValue (looperAnchorKey, 0));
    looperSpectrum = file->getBoolValue (looperSpectrumKey, false);
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

void TransportSettings::setTapAutoCommitEnabled (bool enabled)
{
    if (tapAutoCommit == enabled)
        return;

    tapAutoCommit = enabled;
    writeValue (tapAutoCommitKey, enabled);
}

void TransportSettings::setTapResetHoldSeconds (double seconds)
{
    const auto clamped = juce::jlimit (minTapResetHold, maxTapResetHold, seconds);

    if (juce::exactlyEqual (clamped, tapResetHoldSeconds))
        return;

    tapResetHoldSeconds = clamped;
    writeValue (tapResetHoldKey, clamped);
}

void TransportSettings::setMetronomeEnabled (bool enabled)
{
    if (metronome == enabled)
        return;

    metronome = enabled;
    writeValue (metronomeKey, enabled);
}

void TransportSettings::setMetronomeAnchor (int pairIndex)
{
    const auto clamped = juce::jlimit (0, 31, pairIndex);

    if (metronomeAnchor == clamped)
        return;

    metronomeAnchor = clamped;
    writeValue (metronomeAnchorKey, clamped);
}

void TransportSettings::setLooperSource (const juce::String& sourceKey)
{
    const auto effective = sourceKey.isEmpty() ? juce::String ("master") : sourceKey;

    if (looperSource == effective)
        return;

    looperSource = effective;
    writeValue (looperSourceKey, effective);
}

void TransportSettings::setLooperAnchor (int pairIndex)
{
    // −1 = „Kein Master-Out" (Looper-I/O 07/2026) — Looper laufen weiter,
    // nur die additive Anker-Ausgabe entfällt (LooperBank::mixToOutput)
    const auto clamped = juce::jlimit (-1, 31, pairIndex);

    if (looperAnchor == clamped)
        return;

    looperAnchor = clamped;
    writeValue (looperAnchorKey, clamped);
}

void TransportSettings::setLooperSpectrumEnabled (bool enabled)
{
    if (looperSpectrum == enabled)
        return;

    looperSpectrum = enabled;
    writeValue (looperSpectrumKey, enabled);
}

} // namespace conduit
