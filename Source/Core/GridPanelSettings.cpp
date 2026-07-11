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
