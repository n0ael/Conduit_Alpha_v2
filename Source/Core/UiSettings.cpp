#include "UiSettings.h"

namespace conduit
{

namespace
{
    constexpr const char* uiScaleKey   = "uiScale";
    constexpr const char* fontScaleKey = "fontScale";
    constexpr const char* devModeKey   = "devModeEnabled";
    constexpr const char* dspMeterKey  = "dspMeterEnabled";
    constexpr const char* softKeyboardKey = "softKeyboardEnabled";
}

//==============================================================================
juce::PropertiesFile::Options UiSettings::defaultOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "Ui";
    options.filenameSuffix      = ".settings";
    options.folderName          = "Conduit";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

//==============================================================================
UiSettings::UiSettings (const juce::PropertiesFile::Options& options)
{
    applicationProperties.setStorageParameters (options);
    loadFromFile();
}

UiSettings::~UiSettings()
{
    if (auto* file = applicationProperties.getUserSettings())
        file->saveIfNeeded();
}

//==============================================================================
void UiSettings::loadFromFile()
{
    if (auto* file = applicationProperties.getUserSettings())
    {
        // Auch beim Laden clampen — eine handeditierte/defekte Datei darf
        // nie eine unbenutzbare (z.B. 10x skalierte) Oberfläche erzeugen
        uiScale = juce::jlimit (minUiScale, maxUiScale,
            static_cast<float> (file->getDoubleValue (uiScaleKey, defaultUiScale)));
        fontScale = juce::jlimit (minFontScale, maxFontScale,
            static_cast<float> (file->getDoubleValue (fontScaleKey, defaultFontScale)));
        devModeEnabled  = file->getBoolValue (devModeKey, false);
        dspMeterEnabled = file->getBoolValue (dspMeterKey, true);
        softKeyboardEnabled = file->getBoolValue (softKeyboardKey,
                                                  defaultSoftKeyboardEnabled);
    }
}

void UiSettings::setUiScale (float scale)
{
    const auto clamped = juce::jlimit (minUiScale, maxUiScale, scale);

    if (juce::exactlyEqual (clamped, uiScale))
        return;

    uiScale = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (uiScaleKey, static_cast<double> (clamped));
        file->saveIfNeeded();
    }

    sendChangeMessage();
}

void UiSettings::setFontScale (float scale)
{
    const auto clamped = juce::jlimit (minFontScale, maxFontScale, scale);

    if (juce::exactlyEqual (clamped, fontScale))
        return;

    fontScale = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (fontScaleKey, static_cast<double> (clamped));
        file->saveIfNeeded();
    }

    sendChangeMessage();
}

void UiSettings::setDevModeEnabled (bool enabled)
{
    if (enabled == devModeEnabled)
        return;

    devModeEnabled = enabled;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (devModeKey, enabled);
        file->saveIfNeeded();
    }

    sendChangeMessage();
}

void UiSettings::setDspMeterEnabled (bool enabled)
{
    if (enabled == dspMeterEnabled)
        return;

    dspMeterEnabled = enabled;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (dspMeterKey, enabled);
        file->saveIfNeeded();
    }

    sendChangeMessage();
}

void UiSettings::setSoftKeyboardEnabled (bool enabled)
{
    if (enabled == softKeyboardEnabled)
        return;

    softKeyboardEnabled = enabled;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (softKeyboardKey, enabled);
        file->saveIfNeeded();
    }

    sendChangeMessage();
}

} // namespace conduit
