#include "GridSettingsView.h"

#include <utility>

#include "GridPage.h"
#include "Modules/ConduitModule.h"
#include "PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr int kPaddingX      = 8;
    constexpr int kSectionGap    = 10;
    constexpr int kHeadingHeight = 18;
    constexpr int kRowHeight     = 32;
    constexpr int kRowGap        = 4;
    constexpr int kTileGap       = 6;
}

GridSettingsView::GridSettingsView (juce::ValueTree rootStateToUse, grid::MidiDeviceTarget& midiTargetToUse,
                                    GridPanelSettings& panelSettingsToUse,
                                    grid::InTuneLocation initialInTuneLocation,
                                    float initialInTuneWidthPercent,
                                    grid::ExpressionMode initialExpressionMode)
    : rootState (std::move (rootStateToUse)), midiTarget (midiTargetToUse), panelSettings (panelSettingsToUse),
      inTuneWidthField { NumberFieldBracket::Config { 0.0, 100.0, (double) initialInTuneWidthPercent,
                                                      1.0, 0, 0.5, "Width" } },
      systemRowsField  { NumberFieldBracket::Config {
          (double) GridPanelSettings::minSystemControlRows, (double) GridPanelSettings::maxSystemControlRows,
          (double) panelSettingsToUse.getSystemControlRows(), 1.0, 0, 0.05, "Rows" } },
      ribbonWidthField { NumberFieldBracket::Config {
          (double) GridPanelSettings::minRibbonWidthPx, (double) GridPanelSettings::maxRibbonWidthPx,
          (double) panelSettingsToUse.getRibbonWidthPx(), 1.0, 0, 1.0, "Width" } }
{
    addAndMakeVisible (outputCombo);
    addAndMakeVisible (rootTile);
    addAndMakeVisible (scaleTile);
    addAndMakeVisible (inTuneLocationPadTile);
    addAndMakeVisible (inTuneLocationFingerTile);
    addAndMakeVisible (inTuneWidthField);
    addAndMakeVisible (expressionMpeTile);
    addAndMakeVisible (expressionPolyTile);
    addAndMakeVisible (expressionMonoTile);
    addAndMakeVisible (systemRowsField);
    addAndMakeVisible (ribbonWidthField);
    addAndMakeVisible (modwheelToggle);
    addAndMakeVisible (modwheelLabel);

    modwheelLabel.setJustificationType (juce::Justification::centredLeft);
    modwheelLabel.setColour (juce::Label::textColourId, push::colours::textDim);

    rebuildDeviceList();
    outputCombo.onChange = [this] { handleDeviceSelected(); };

    rootTile.onClick = [this]
    {
        const auto current = juce::jlimit (0, 11, (int) rootState.getProperty (id::scaleRoot, 0));
        rootState.setProperty (id::scaleRoot, GridPage::nextScaleRoot (current), nullptr);
    };
    scaleTile.onClick = [this]
    {
        const auto current = scaleTypeFromString (rootState.getProperty (id::scaleType).toString());
        rootState.setProperty (id::scaleType, toString (GridPage::nextScaleType (current)), nullptr);
    };
    rootState.addListener (this);
    refreshScaleLabels();

    // In-Tune Location (Block B1): exklusives Zwei-Tile-Paar.
    inTuneLocationPadTile.setActive (initialInTuneLocation == grid::InTuneLocation::pad);
    inTuneLocationFingerTile.setActive (initialInTuneLocation == grid::InTuneLocation::finger);
    inTuneLocationPadTile.onClick = [this]
    {
        inTuneLocationPadTile.setActive (true);
        inTuneLocationFingerTile.setActive (false);
        if (onInTuneLocationChanged != nullptr)
            onInTuneLocationChanged (grid::InTuneLocation::pad);
    };
    inTuneLocationFingerTile.onClick = [this]
    {
        inTuneLocationPadTile.setActive (false);
        inTuneLocationFingerTile.setActive (true);
        if (onInTuneLocationChanged != nullptr)
            onInTuneLocationChanged (grid::InTuneLocation::finger);
    };

    inTuneWidthField.onValueChanged = [this] (double v)
    {
        if (onInTuneWidthChanged != nullptr)
            onInTuneWidthChanged ((float) v);
    };

    // Expression Mode (Block B4): exklusives Drei-Tile-Set.
    const auto setExpressionTiles = [this] (grid::ExpressionMode mode)
    {
        expressionMpeTile.setActive (mode == grid::ExpressionMode::mpe);
        expressionPolyTile.setActive (mode == grid::ExpressionMode::polyAftertouch);
        expressionMonoTile.setActive (mode == grid::ExpressionMode::monoAftertouch);
    };
    setExpressionTiles (initialExpressionMode);

    expressionMpeTile.onClick = [this, setExpressionTiles]
    {
        setExpressionTiles (grid::ExpressionMode::mpe);
        if (onExpressionModeChanged != nullptr)
            onExpressionModeChanged (grid::ExpressionMode::mpe);
    };
    expressionPolyTile.onClick = [this, setExpressionTiles]
    {
        setExpressionTiles (grid::ExpressionMode::polyAftertouch);
        if (onExpressionModeChanged != nullptr)
            onExpressionModeChanged (grid::ExpressionMode::polyAftertouch);
    };
    expressionMonoTile.onClick = [this, setExpressionTiles]
    {
        setExpressionTiles (grid::ExpressionMode::monoAftertouch);
        if (onExpressionModeChanged != nullptr)
            onExpressionModeChanged (grid::ExpressionMode::monoAftertouch);
    };

    // Layout (Edit-Grid-Ersatz, Block D1) -- direkt in GridPanelSettings
    // persistiert, Besitzer relayoutet auf onLayoutSettingsChanged.
    systemRowsField.onValueCommitted = [this] (double v)
    {
        panelSettings.setSystemControlRows ((int) v);
        if (onLayoutSettingsChanged != nullptr)
            onLayoutSettingsChanged();
    };
    ribbonWidthField.onValueCommitted = [this] (double v)
    {
        panelSettings.setRibbonWidthPx ((int) v);
        if (onLayoutSettingsChanged != nullptr)
            onLayoutSettingsChanged();
    };

    modwheelToggle.setActive (panelSettings.isModwheelEnabled());
    modwheelToggle.onClick = [this]
    {
        const auto shouldEnable = ! modwheelToggle.isActive();
        modwheelToggle.setActive (shouldEnable);
        panelSettings.setModwheelEnabled (shouldEnable);

        if (onModwheelToggled != nullptr)
            onModwheelToggled (shouldEnable);
    };
}

GridSettingsView::~GridSettingsView()
{
    rootState.removeListener (this);
}

void GridSettingsView::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == rootState && (property == id::scaleRoot || property == id::scaleType))
        refreshScaleLabels();
}

void GridSettingsView::refreshScaleLabels()
{
    const auto rootNote = juce::jlimit (0, 11, (int) rootState.getProperty (id::scaleRoot, 0));
    const auto type = scaleTypeFromString (rootState.getProperty (id::scaleType).toString());

    rootTile.setText (GridPage::noteNameFor (rootNote));
    scaleTile.setText (GridPage::scaleDisplayNameFor (type));
}

void GridSettingsView::rebuildDeviceList()
{
    devices = grid::MidiDeviceTarget::availableDevices();

    outputCombo.clear (juce::dontSendNotification);
    outputCombo.addItem ("Kein Ausgang", 1);

    for (int i = 0; i < devices.size(); ++i)
        outputCombo.addItem (devices.getReference (i).name, i + 2);

    outputCombo.setSelectedId (1, juce::dontSendNotification);
}

void GridSettingsView::handleDeviceSelected()
{
    const auto selectedId = outputCombo.getSelectedId();

    if (selectedId <= 1)
    {
        midiTarget.closeDevice();
        return;
    }

    const auto index = selectedId - 2;
    if (index >= 0 && index < devices.size())
        midiTarget.openDevice (devices.getReference (index).identifier);
}

void GridSettingsView::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::panel);

    g.setColour (push::colours::textDim);
    g.setFont (push::scaledFont (11.0f, true));

    const std::pair<juce::Rectangle<int>, const char*> headings[] {
        { pitchHeadingBounds,      "Pitch" },
        { expressionHeadingBounds, "Expression" },
        { layoutHeadingBounds,     "Layout" },
        { modwheelHeadingBounds,   "Modwheel" },
    };

    for (const auto& [bounds, text] : headings)
        if (! bounds.isEmpty())
            g.drawText (text, bounds, juce::Justification::centredLeft);
}

void GridSettingsView::resized()
{
    auto area = getLocalBounds().reduced (kPaddingX, 8);

    // Performance-Slide-Out: MIDI-Port + Skala-Kacheln in einer Zeile.
    auto slideOutRow = area.removeFromTop (kRowHeight);
    outputCombo.setBounds (slideOutRow.removeFromLeft (juce::jmax (80, slideOutRow.getWidth() - 152)));
    slideOutRow.removeFromLeft (6);
    rootTile.setBounds (slideOutRow.removeFromLeft (44));
    slideOutRow.removeFromLeft (4);
    scaleTile.setBounds (slideOutRow);
    area.removeFromTop (kSectionGap);

    // Pitch: In-Tune Location (zwei Tiles) + Width-Feld.
    pitchHeadingBounds = area.removeFromTop (kHeadingHeight);
    auto inTuneLocationRow = area.removeFromTop (kRowHeight);
    const auto locationTileWidth = (inTuneLocationRow.getWidth() - kTileGap) / 2;
    inTuneLocationPadTile.setBounds (inTuneLocationRow.removeFromLeft (locationTileWidth));
    inTuneLocationRow.removeFromLeft (kTileGap);
    inTuneLocationFingerTile.setBounds (inTuneLocationRow);
    area.removeFromTop (kRowGap);
    inTuneWidthField.setBounds (area.removeFromTop (kRowHeight));
    area.removeFromTop (kSectionGap);

    // Expression Mode: drei Tiles nebeneinander.
    expressionHeadingBounds = area.removeFromTop (kHeadingHeight);
    auto expressionRow = area.removeFromTop (kRowHeight);
    const auto expressionTileWidth = (expressionRow.getWidth() - kTileGap * 2) / 3;
    expressionMpeTile.setBounds (expressionRow.removeFromLeft (expressionTileWidth));
    expressionRow.removeFromLeft (kTileGap);
    expressionPolyTile.setBounds (expressionRow.removeFromLeft (expressionTileWidth));
    expressionRow.removeFromLeft (kTileGap);
    expressionMonoTile.setBounds (expressionRow);
    area.removeFromTop (kSectionGap);

    // Layout: XY-Zeilen + Fader-Breite (Edit-Grid-Ersatz).
    layoutHeadingBounds = area.removeFromTop (kHeadingHeight);
    systemRowsField.setBounds (area.removeFromTop (kRowHeight));
    area.removeFromTop (kRowGap);
    ribbonWidthField.setBounds (area.removeFromTop (kRowHeight));
    area.removeFromTop (kSectionGap);

    // Modwheel-Toggle + Label.
    modwheelHeadingBounds = area.removeFromTop (kHeadingHeight);
    auto modwheelRow = area.removeFromTop (juce::jmax (LockToggle::kComponentSize, kRowHeight));
    modwheelToggle.setBounds (modwheelRow.removeFromLeft (LockToggle::kComponentSize));
    modwheelLabel.setBounds (modwheelRow.reduced (8, 0));
}

} // namespace conduit
