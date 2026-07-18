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
    constexpr const char* uiFpsLimitKey   = "uiFpsLimit";
    constexpr const char* interactionMinZoomKey = "interactionMinZoom";
    constexpr const char* pinchDeadZoneKey      = "pinchDeadZone";
    constexpr const char* zoomStrengthKey       = "zoomStrength";
    constexpr const char* zoomCurveKey          = "zoomCurve";
    constexpr const char* gestureSmoothingKey   = "gestureSmoothing";
    constexpr const char* workZoomKey           = "canvasWorkZoom";
    constexpr const char* birdeyeZoomKey        = "canvasBirdeyeZoom";

    /** Erlaubte Modi: 120 (Nativ, max) | 60 | 30 — alles andere wird auf
        den nächsten Modus geklemmt (handeditierte Datei, alte Versionen). */
    int clampFpsLimit (int limitFps) noexcept
    {
        if (limitFps >= 90) return 120;
        if (limitFps >= 45) return 60;
        return 30;
    }
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
        uiFpsLimit = clampFpsLimit (file->getIntValue (uiFpsLimitKey, defaultUiFpsLimit));
        interactionMinZoom = juce::jlimit (minInteractionMinZoom, maxInteractionMinZoom,
            static_cast<float> (file->getDoubleValue (interactionMinZoomKey,
                                                      defaultInteractionMinZoom)));
        pinchDeadZone = juce::jlimit (minPinchDeadZone, maxPinchDeadZone,
            static_cast<float> (file->getDoubleValue (pinchDeadZoneKey,
                                                      defaultPinchDeadZone)));
        zoomStrength = juce::jlimit (minZoomStrength, maxZoomStrength,
            static_cast<float> (file->getDoubleValue (zoomStrengthKey,
                                                      defaultZoomStrength)));
        zoomCurve = juce::jlimit (minZoomCurve, maxZoomCurve,
            static_cast<float> (file->getDoubleValue (zoomCurveKey, defaultZoomCurve)));
        gestureSmoothing = juce::jlimit (minGestureSmoothing, maxGestureSmoothing,
            static_cast<float> (file->getDoubleValue (gestureSmoothingKey,
                                                      defaultGestureSmoothing)));
        workZoom = juce::jlimit (minWorkZoom, maxWorkZoom,
            static_cast<float> (file->getDoubleValue (workZoomKey, defaultWorkZoom)));
        birdeyeZoom = juce::jlimit (minBirdeyeZoom, maxBirdeyeZoom,
            static_cast<float> (file->getDoubleValue (birdeyeZoomKey, defaultBirdeyeZoom)));
    }
}

void UiSettings::setUiFpsLimit (int limitFps)
{
    const auto clamped = clampFpsLimit (limitFps);

    if (clamped == uiFpsLimit)
        return;

    uiFpsLimit = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (uiFpsLimitKey, clamped);
        file->saveIfNeeded();
    }

    sendChangeMessage();
}

void UiSettings::setInteractionMinZoom (float zoomThreshold)
{
    const auto clamped = juce::jlimit (minInteractionMinZoom, maxInteractionMinZoom,
                                       zoomThreshold);

    if (juce::exactlyEqual (clamped, interactionMinZoom))
        return;

    interactionMinZoom = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (interactionMinZoomKey, static_cast<double> (clamped));
        file->saveIfNeeded();
    }

    sendChangeMessage();
}

void UiSettings::setPinchDeadZone (float spreadFraction)
{
    const auto clamped = juce::jlimit (minPinchDeadZone, maxPinchDeadZone, spreadFraction);

    if (juce::exactlyEqual (clamped, pinchDeadZone))
        return;

    pinchDeadZone = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (pinchDeadZoneKey, static_cast<double> (clamped));
        file->saveIfNeeded();
    }

    sendChangeMessage();
}

void UiSettings::setZoomStrength (float gain)
{
    const auto clamped = juce::jlimit (minZoomStrength, maxZoomStrength, gain);

    if (juce::exactlyEqual (clamped, zoomStrength))
        return;

    zoomStrength = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (zoomStrengthKey, static_cast<double> (clamped));
        file->saveIfNeeded();
    }

    sendChangeMessage();
}

void UiSettings::setZoomCurve (float exponent)
{
    const auto clamped = juce::jlimit (minZoomCurve, maxZoomCurve, exponent);

    if (juce::exactlyEqual (clamped, zoomCurve))
        return;

    zoomCurve = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (zoomCurveKey, static_cast<double> (clamped));
        file->saveIfNeeded();
    }

    sendChangeMessage();
}

void UiSettings::setGestureSmoothing (float amount)
{
    const auto clamped = juce::jlimit (minGestureSmoothing, maxGestureSmoothing, amount);

    if (juce::exactlyEqual (clamped, gestureSmoothing))
        return;

    gestureSmoothing = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (gestureSmoothingKey, static_cast<double> (clamped));
        file->saveIfNeeded();
    }

    sendChangeMessage();
}

void UiSettings::setWorkZoom (float level)
{
    const auto clamped = juce::jlimit (minWorkZoom, maxWorkZoom, level);

    if (juce::exactlyEqual (clamped, workZoom))
        return;

    workZoom = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (workZoomKey, static_cast<double> (clamped));
        file->saveIfNeeded();
    }

    sendChangeMessage();
}

void UiSettings::setBirdeyeZoom (float level)
{
    const auto clamped = juce::jlimit (minBirdeyeZoom, maxBirdeyeZoom, level);

    if (juce::exactlyEqual (clamped, birdeyeZoom))
        return;

    birdeyeZoom = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (birdeyeZoomKey, static_cast<double> (clamped));
        file->saveIfNeeded();
    }

    sendChangeMessage();
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
