#include "GridPanelSettings.h"

#include "UI/PushLookAndFeel.h"   // Default-Achsenfarben (push::colours-Tokens)

namespace conduit
{

namespace
{
    constexpr const char* editorPanelWidthKey     = "editorPanelWidth";
    constexpr const char* editorPanelOpenKey      = "editorPanelOpen";
    constexpr const char* editorThresholdWidthKey = "editorThresholdWidth";
    constexpr const char* noteCircleFadeMsKey     = "noteCircleFadeMs";
    constexpr const char* gridLayoutModeKey       = "gridLayoutMode";
    constexpr const char* systemControlRowsKey    = "systemControlRows";
    constexpr const char* ribbonWidthPxKey        = "ribbonWidthPx";
    constexpr const char* modwheelEnabledKey      = "modwheelEnabled";
    constexpr const char* masterMidiInputNameKey  = "masterMidiInputName";
    constexpr const char* gridMidiInputNameKey    = "gridMidiInputName";
    constexpr const char* masterMidiFavouritesKey = "masterMidiFavourites";
    constexpr const char* trackTabsBottomKey      = "trackTabsBottom";
    constexpr const char* trackTabsFontPxKey      = "trackTabsFontPx";
    constexpr const char* trackTabMinWidthPxKey   = "trackTabMinWidthPx";
    constexpr const char* rootPadTrackColourKey   = "rootPadTrackColour";
    constexpr const char* gridGravityEnabledKey   = "gridGravityEnabled";
    constexpr const char* physicsForceKey         = "physicsForce";
    constexpr const char* physicsMassKey          = "physicsMass";
    constexpr const char* physicsInertiaKey       = "physicsInertia";
    constexpr const char* gravityDelayMsKey       = "gravityDelayMs";
    constexpr const char* gravityThresholdKey     = "gravityThresholdPadsPerSec";
    constexpr const char* gravityFadeMsKey        = "gravityForceFadeMs";
    constexpr const char* controlPhysicsEnabledKey = "controlPhysicsEnabled";
    constexpr const char* controlSnapToDefaultKey  = "controlSnapToDefault";
    constexpr const char* pressureSensitivityKey  = "pressureSensitivity";
    constexpr const char* slideSensitivityKey     = "slideSensitivity";
    constexpr const char* bendRangeIndexKey       = "bendRangeIndex";
    constexpr const char* inTuneLocationPadKey    = "inTuneLocationPad";
    constexpr const char* inTuneWidthPercentKey   = "inTuneWidthPercent";
    constexpr const char* expressionModeIndexKey  = "expressionModeIndex";
    constexpr const char* octaveShiftKey          = "octaveShift";
    constexpr const char* gridMidiOutDeviceKey    = "gridMidiOutDeviceName";
    constexpr const char* controlMidiInDeviceKey  = "controlMidiInDeviceName";
    constexpr const char* echoMidiInDeviceKey     = "echoMidiInDeviceName";

    // Achsen-Farben (Grid-Page v2) — Index = GridPanelSettings::axisIndex
    // (Pressure 0, Slide 1, PitchBend 2).
    constexpr const char* axisColourKeys[] = { "axisColourPressure",
                                               "axisColourSlide",
                                               "axisColourPitchBend" };

    [[nodiscard]] std::array<juce::Colour, 3> defaultAxisColours()
    {
        return { push::colours::ledOrange, push::colours::ledCyan, push::colours::ledGreen };
    }
}

//==============================================================================
juce::PropertiesFile::Options GridPanelSettings::defaultOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "GridPanel";
    options.filenameSuffix      = ".settings";
    options.folderName          = "Conduit";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

//==============================================================================
GridPanelSettings::GridPanelSettings (const juce::PropertiesFile::Options& options)
{
    applicationProperties.setStorageParameters (options);
    loadFromFile();
}

GridPanelSettings::~GridPanelSettings()
{
    if (auto* file = applicationProperties.getUserSettings())
        file->saveIfNeeded();
}

//==============================================================================
void GridPanelSettings::loadFromFile()
{
    axisColours = defaultAxisColours();

    if (auto* file = applicationProperties.getUserSettings())
    {
        editorPanelWidth = file->getIntValue (editorPanelWidthKey, defaultWidth);
        editorPanelOpen  = file->getBoolValue (editorPanelOpenKey, false);

        editorThresholdWidth = juce::jlimit (minThresholdWidth, maxThresholdWidth,
            file->getIntValue (editorThresholdWidthKey, defaultThresholdWidth));
        noteCircleFadeMs = juce::jlimit (minNoteCircleFadeMs, maxNoteCircleFadeMs,
            file->getIntValue (noteCircleFadeMsKey, defaultNoteCircleFadeMs));

        // Unbekannte Werte fallen defensiv auf fullPads (Default 0) zurück.
        gridLayoutMode = file->getIntValue (gridLayoutModeKey, 0) == 1
                             ? GridLayoutMode::xyFaders
                             : GridLayoutMode::fullPads;

        systemControlRows = juce::jlimit (minSystemControlRows, maxSystemControlRows,
            file->getIntValue (systemControlRowsKey, defaultSystemControlRows));
        ribbonWidthPx = juce::jlimit (minRibbonWidthPx, maxRibbonWidthPx,
            file->getIntValue (ribbonWidthPxKey, defaultRibbonWidthPx));
        modwheelEnabled = file->getBoolValue (modwheelEnabledKey, false);
        masterMidiInputName = file->getValue (masterMidiInputNameKey, {});
        gridMidiInputName   = file->getValue (gridMidiInputNameKey, {});
        masterMidiFavourites.addTokens (file->getValue (masterMidiFavouritesKey, {}),
                                        ";", {});
        masterMidiFavourites.removeEmptyStrings();

        trackTabsBottom = file->getBoolValue (trackTabsBottomKey, false);
        trackTabsFontPx = juce::jlimit (minTrackTabsFontPx, maxTrackTabsFontPx,
            file->getIntValue (trackTabsFontPxKey, defaultTrackTabsFontPx));
        trackTabMinWidthPx = juce::jlimit (minTrackTabMinWidthPx, maxTrackTabMinWidthPx,
            file->getIntValue (trackTabMinWidthPxKey, defaultTrackTabMinWidthPx));
        rootPadTrackColour = file->getBoolValue (rootPadTrackColourKey, true);

        gridGravityEnabled = file->getBoolValue (gridGravityEnabledKey, false);
        physicsForce = juce::jlimit (minPhysicsForce, maxPhysicsForce,
            file->getDoubleValue (physicsForceKey, defaultPhysicsForce));
        physicsMass = juce::jlimit (minPhysicsMass, maxPhysicsMass,
            file->getDoubleValue (physicsMassKey, defaultPhysicsMass));
        physicsInertia = juce::jlimit (minPhysicsInertia, maxPhysicsInertia,
            file->getIntValue (physicsInertiaKey, defaultPhysicsInertia));
        gravityDelayMs = juce::jlimit (minGravityDelayMs, maxGravityDelayMs,
            file->getIntValue (gravityDelayMsKey, defaultGravityDelayMs));
        gravityThreshold = juce::jlimit (minGravityThreshold, maxGravityThreshold,
            file->getDoubleValue (gravityThresholdKey, defaultGravityThreshold));
        gravityFadeMs = juce::jlimit (minGravityFadeMs, maxGravityFadeMs,
            file->getIntValue (gravityFadeMsKey, defaultGravityFadeMs));
        controlPhysicsEnabled = file->getBoolValue (controlPhysicsEnabledKey, false);
        controlSnapToDefault  = file->getBoolValue (controlSnapToDefaultKey, false);

        pressureSensitivity = juce::jlimit (0.0, 100.0,
            file->getDoubleValue (pressureSensitivityKey, defaultSensitivity));
        slideSensitivity = juce::jlimit (0.0, 100.0,
            file->getDoubleValue (slideSensitivityKey, defaultSensitivity));
        bendRangeIndex = juce::jlimit (minBendRangeIndex, maxBendRangeIndex,
            file->getIntValue (bendRangeIndexKey, defaultBendRangeIndex));
        inTuneLocationPad = file->getBoolValue (inTuneLocationPadKey, true);
        inTuneWidthPercent = juce::jlimit (0.0, 100.0,
            file->getDoubleValue (inTuneWidthPercentKey, defaultInTuneWidthPercent));
        expressionModeIndex = juce::jlimit (0, 2,
            file->getIntValue (expressionModeIndexKey, 0));
        octaveShift = juce::jlimit (-maxOctaveShift, maxOctaveShift,
            file->getIntValue (octaveShiftKey, 0));

        gridMidiOutDeviceName   = file->getValue (gridMidiOutDeviceKey, {});
        controlMidiInDeviceName = file->getValue (controlMidiInDeviceKey, {});
        echoMidiInDeviceName    = file->getValue (echoMidiInDeviceKey, {});

        for (size_t i = 0; i < axisColours.size(); ++i)
        {
            const auto text = file->getValue (axisColourKeys[i], axisColours[i].toString());
            if (text.isNotEmpty())
                axisColours[i] = juce::Colour::fromString (text);
        }
    }
}

void GridPanelSettings::setEditorPanelWidth (int newWidth)
{
    if (newWidth == editorPanelWidth)
        return;

    editorPanelWidth = newWidth;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (editorPanelWidthKey, editorPanelWidth);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setEditorPanelOpen (bool shouldBeOpen)
{
    if (shouldBeOpen == editorPanelOpen)
        return;

    editorPanelOpen = shouldBeOpen;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (editorPanelOpenKey, editorPanelOpen);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setEditorThresholdWidth (int newWidth)
{
    const auto clamped = juce::jlimit (minThresholdWidth, maxThresholdWidth, newWidth);

    if (clamped == editorThresholdWidth)
        return;

    editorThresholdWidth = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (editorThresholdWidthKey, editorThresholdWidth);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setNoteCircleFadeMs (int newFadeMs)
{
    const auto clamped = juce::jlimit (minNoteCircleFadeMs, maxNoteCircleFadeMs, newFadeMs);

    if (clamped == noteCircleFadeMs)
        return;

    noteCircleFadeMs = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (noteCircleFadeMsKey, noteCircleFadeMs);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setGridLayoutMode (GridLayoutMode newMode)
{
    if (newMode == gridLayoutMode)
        return;

    gridLayoutMode = newMode;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (gridLayoutModeKey, (int) gridLayoutMode);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setSystemControlRows (int newRows)
{
    const auto clamped = juce::jlimit (minSystemControlRows, maxSystemControlRows, newRows);

    if (clamped == systemControlRows)
        return;

    systemControlRows = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (systemControlRowsKey, systemControlRows);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setRibbonWidthPx (int newWidthPx)
{
    const auto clamped = juce::jlimit (minRibbonWidthPx, maxRibbonWidthPx, newWidthPx);

    if (clamped == ribbonWidthPx)
        return;

    ribbonWidthPx = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (ribbonWidthPxKey, ribbonWidthPx);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setModwheelEnabled (bool shouldBeEnabled)
{
    if (shouldBeEnabled == modwheelEnabled)
        return;

    modwheelEnabled = shouldBeEnabled;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (modwheelEnabledKey, modwheelEnabled);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setMasterMidiInputName (const juce::String& newName)
{
    if (newName == masterMidiInputName)
        return;

    masterMidiInputName = newName;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (masterMidiInputNameKey, masterMidiInputName);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setGridMidiInputName (const juce::String& newName)
{
    if (newName == gridMidiInputName)
        return;

    gridMidiInputName = newName;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (gridMidiInputNameKey, gridMidiInputName);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setMasterMidiFavourites (const juce::StringArray& newFavourites)
{
    if (newFavourites == masterMidiFavourites)
        return;

    masterMidiFavourites = newFavourites;
    masterMidiFavourites.removeEmptyStrings();

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (masterMidiFavouritesKey,
                        masterMidiFavourites.joinIntoString (";"));
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setTrackTabsBottom (bool shouldBeBottom)
{
    if (shouldBeBottom == trackTabsBottom)
        return;

    trackTabsBottom = shouldBeBottom;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (trackTabsBottomKey, trackTabsBottom);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setTrackTabsFontPx (int newFontPx)
{
    const auto clamped = juce::jlimit (minTrackTabsFontPx, maxTrackTabsFontPx, newFontPx);

    if (clamped == trackTabsFontPx)
        return;

    trackTabsFontPx = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (trackTabsFontPxKey, trackTabsFontPx);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setTrackTabMinWidthPx (int newWidthPx)
{
    const auto clamped = juce::jlimit (minTrackTabMinWidthPx, maxTrackTabMinWidthPx, newWidthPx);

    if (clamped == trackTabMinWidthPx)
        return;

    trackTabMinWidthPx = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (trackTabMinWidthPxKey, trackTabMinWidthPx);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setRootPadTrackColour (bool shouldUseTrackColour)
{
    if (shouldUseTrackColour == rootPadTrackColour)
        return;

    rootPadTrackColour = shouldUseTrackColour;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (rootPadTrackColourKey, rootPadTrackColour);
        file->saveIfNeeded();
    }
}

//==============================================================================
// Block J (Physics): Toggles + Feder-/Gravity-Tuning.

void GridPanelSettings::setGridGravityEnabled (bool shouldBeEnabled)
{
    if (shouldBeEnabled == gridGravityEnabled)
        return;

    gridGravityEnabled = shouldBeEnabled;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (gridGravityEnabledKey, gridGravityEnabled);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setPhysicsForce (double newForce)
{
    const auto clamped = juce::jlimit (minPhysicsForce, maxPhysicsForce, newForce);

    if (juce::exactlyEqual (clamped, physicsForce))
        return;

    physicsForce = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (physicsForceKey, physicsForce);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setPhysicsMass (double newMass)
{
    const auto clamped = juce::jlimit (minPhysicsMass, maxPhysicsMass, newMass);

    if (juce::exactlyEqual (clamped, physicsMass))
        return;

    physicsMass = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (physicsMassKey, physicsMass);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setPhysicsInertia (int newInertia)
{
    const auto clamped = juce::jlimit (minPhysicsInertia, maxPhysicsInertia, newInertia);

    if (clamped == physicsInertia)
        return;

    physicsInertia = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (physicsInertiaKey, physicsInertia);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setGravityDelayMs (int newDelayMs)
{
    const auto clamped = juce::jlimit (minGravityDelayMs, maxGravityDelayMs, newDelayMs);

    if (clamped == gravityDelayMs)
        return;

    gravityDelayMs = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (gravityDelayMsKey, gravityDelayMs);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setGravityThreshold (double newThreshold)
{
    const auto clamped = juce::jlimit (minGravityThreshold, maxGravityThreshold, newThreshold);

    if (juce::exactlyEqual (clamped, gravityThreshold))
        return;

    gravityThreshold = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (gravityThresholdKey, gravityThreshold);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setGravityFadeMs (int newFadeMs)
{
    const auto clamped = juce::jlimit (minGravityFadeMs, maxGravityFadeMs, newFadeMs);

    if (clamped == gravityFadeMs)
        return;

    gravityFadeMs = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (gravityFadeMsKey, gravityFadeMs);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setControlPhysicsEnabled (bool shouldBeEnabled)
{
    if (shouldBeEnabled == controlPhysicsEnabled)
        return;

    controlPhysicsEnabled = shouldBeEnabled;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (controlPhysicsEnabledKey, controlPhysicsEnabled);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setControlSnapToDefault (bool shouldSnap)
{
    if (shouldSnap == controlSnapToDefault)
        return;

    controlSnapToDefault = shouldSnap;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (controlSnapToDefaultKey, controlSnapToDefault);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setGridMidiOutDeviceName (const juce::String& newName)
{
    if (newName == gridMidiOutDeviceName)
        return;

    gridMidiOutDeviceName = newName;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (gridMidiOutDeviceKey, gridMidiOutDeviceName);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setControlMidiInDeviceName (const juce::String& newName)
{
    if (newName == controlMidiInDeviceName)
        return;

    controlMidiInDeviceName = newName;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (controlMidiInDeviceKey, controlMidiInDeviceName);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setEchoMidiInDeviceName (const juce::String& newName)
{
    if (newName == echoMidiInDeviceName)
        return;

    echoMidiInDeviceName = newName;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (echoMidiInDeviceKey, echoMidiInDeviceName);
        file->saveIfNeeded();
    }
}

juce::File GridPanelSettings::sessionFile()
{
    if (auto* file = applicationProperties.getUserSettings())
        return file->getFile().getSiblingFile ("GridSession.xml");

    return {};
}

//==============================================================================
// Block K: Sensitivity/Bend-Range/In-Tune/Expression/Oktav-Shift.

void GridPanelSettings::setPressureSensitivity (double newSensitivity)
{
    const auto clamped = juce::jlimit (0.0, 100.0, newSensitivity);

    if (juce::exactlyEqual (clamped, pressureSensitivity))
        return;

    pressureSensitivity = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (pressureSensitivityKey, pressureSensitivity);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setSlideSensitivity (double newSensitivity)
{
    const auto clamped = juce::jlimit (0.0, 100.0, newSensitivity);

    if (juce::exactlyEqual (clamped, slideSensitivity))
        return;

    slideSensitivity = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (slideSensitivityKey, slideSensitivity);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setBendRangeIndex (int newIndex)
{
    const auto clamped = juce::jlimit (minBendRangeIndex, maxBendRangeIndex, newIndex);

    if (clamped == bendRangeIndex)
        return;

    bendRangeIndex = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (bendRangeIndexKey, bendRangeIndex);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setInTuneLocationPad (bool shouldBePad)
{
    if (shouldBePad == inTuneLocationPad)
        return;

    inTuneLocationPad = shouldBePad;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (inTuneLocationPadKey, inTuneLocationPad);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setInTuneWidthPercent (double newPercent)
{
    const auto clamped = juce::jlimit (0.0, 100.0, newPercent);

    if (juce::exactlyEqual (clamped, inTuneWidthPercent))
        return;

    inTuneWidthPercent = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (inTuneWidthPercentKey, inTuneWidthPercent);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setExpressionModeIndex (int newIndex)
{
    const auto clamped = juce::jlimit (0, 2, newIndex);

    if (clamped == expressionModeIndex)
        return;

    expressionModeIndex = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (expressionModeIndexKey, expressionModeIndex);
        file->saveIfNeeded();
    }
}

void GridPanelSettings::setOctaveShift (int newShift)
{
    const auto clamped = juce::jlimit (-maxOctaveShift, maxOctaveShift, newShift);

    if (clamped == octaveShift)
        return;

    octaveShift = clamped;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (octaveShiftKey, octaveShift);
        file->saveIfNeeded();
    }
}

//==============================================================================
size_t GridPanelSettings::axisIndex (grid::GridVoiceEngine::Axis axis) noexcept
{
    switch (axis)
    {
        case grid::GridVoiceEngine::Axis::Pressure:  return 0;
        case grid::GridVoiceEngine::Axis::Slide:     return 1;
        case grid::GridVoiceEngine::Axis::PitchBend: return 2;
    }

    jassertfalse;
    return 0;
}

juce::Colour GridPanelSettings::getAxisColour (grid::GridVoiceEngine::Axis axis) const noexcept
{
    return axisColours[axisIndex (axis)];
}

void GridPanelSettings::setAxisColour (grid::GridVoiceEngine::Axis axis, juce::Colour newColour)
{
    const auto index = axisIndex (axis);

    if (axisColours[index] == newColour)
        return;

    axisColours[index] = newColour;

    if (auto* file = applicationProperties.getUserSettings())
    {
        file->setValue (axisColourKeys[index], axisColours[index].toString());
        file->saveIfNeeded();
    }
}

} // namespace conduit
