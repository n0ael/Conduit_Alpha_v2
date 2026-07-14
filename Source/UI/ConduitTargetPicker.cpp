#include "ConduitTargetPicker.h"

#include "Modules/ChassisSchema.h"
#include "PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr int kPickerWidth  = 320;
    constexpr int kPickerHeight = 440;
    constexpr int kHeaderHeight = 44;
}

//==============================================================================
ConduitTargetPicker::ConduitTargetPicker (juce::ValueTree rootStateToUse,
                                          std::vector<GridControlEntry> gridControlsToUse)
    : rootState (std::move (rootStateToUse)), gridControls (std::move (gridControlsToUse))
{
    breadcrumbLabel.setJustificationType (juce::Justification::centredLeft);
    breadcrumbLabel.setColour (juce::Label::textColourId, push::colours::text);
    addAndMakeVisible (breadcrumbLabel);

    backTile.onClick = [this]
    {
        buildRootRows();
        resized();
    };
    addChildComponent (backTile);

    viewport.setViewedComponent (&rowList, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    buildRootRows();
    setSize (kPickerWidth, kPickerHeight);
}

void ConduitTargetPicker::buildRootRows()
{
    rows.clear();
    atRoot = true;
    currentModuleUuid.clear();
    currentModuleName.clear();

    const auto nodes = rootState.getChildWithName (id::nodes);

    if (nodes.getNumChildren() > 0)
        rows.push_back ({ Row::Kind::header, "Module", {}, {}, {} });

    for (int i = 0; i < nodes.getNumChildren(); ++i)
    {
        const auto node = nodes.getChild (i);
        const auto uuid = node.getProperty (id::nodeId).toString();
        const auto name = node.getProperty (id::moduleId).toString();

        // Nur Module mit mappbaren dsp-Parametern anbieten.
        if (uuid.isEmpty() || name.isEmpty()
            || ! node.getChildWithName (id::parameters).isValid())
            continue;

        rows.push_back ({ Row::Kind::module, name, uuid, {}, {} });
    }

    if (! gridControls.empty())
        rows.push_back ({ Row::Kind::header, "Grid-Controls", {}, {}, {} });

    for (const auto& [key, name] : gridControls)
        rows.push_back ({ Row::Kind::gridControl, name, {}, {}, key });

    breadcrumbLabel.setText ("Conduit-Ziel", juce::dontSendNotification);
    backTile.setVisible (false);
    rowList.repaint();
    resized();
}

void ConduitTargetPicker::buildParameterRows (const juce::String& nodeUuid,
                                              const juce::String& moduleName)
{
    rows.clear();
    atRoot = false;
    currentModuleUuid = nodeUuid;
    currentModuleName = moduleName;

    const auto node = rootState.getChildWithName (id::nodes)
                          .getChildWithProperty (id::nodeId, nodeUuid);
    const auto parameters = node.getChildWithName (id::parameters);

    for (int i = 0; i < parameters.getNumChildren(); ++i)
    {
        const auto param = parameters.getChild (i);

        // Nur regelbare dsp-Parameter (Chassis-/Attenuverter-Zeilen und
        // versteckte Spalten bleiben draussen).
        if (ChassisSchema::roleOf (param) != juce::String (ChassisSchema::roleDsp)
            || (bool) param.getProperty (id::paramUiHidden, false))
            continue;

        const auto paramId = param.getProperty (id::paramId).toString();
        if (paramId.isNotEmpty())
            rows.push_back ({ Row::Kind::parameter, paramId, nodeUuid, paramId, {} });
    }

    breadcrumbLabel.setText (moduleName, juce::dontSendNotification);
    backTile.setVisible (true);
    rowList.repaint();
    resized();
}

void ConduitTargetPicker::rowTapped (const Row& row)
{
    switch (row.kind)
    {
        case Row::Kind::header:
            return;

        case Row::Kind::module:
            buildParameterRows (row.nodeUuid, row.label);
            return;

        case Row::Kind::parameter:
            if (onParamChosen != nullptr)
                onParamChosen (row.nodeUuid, row.paramId,
                               currentModuleName + ": " + row.paramId);
            dismissSelf();
            return;

        case Row::Kind::gridControl:
            if (onControlChosen != nullptr)
                onControlChosen (row.controlKey, row.label);
            dismissSelf();
            return;
    }
}

void ConduitTargetPicker::dismissSelf()
{
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

void ConduitTargetPicker::resized()
{
    auto bounds = getLocalBounds().reduced (8);

    auto header = bounds.removeFromTop (kHeaderHeight - 8);
    if (backTile.isVisible())
    {
        backTile.setBounds (header.removeFromLeft (36));
        header.removeFromLeft (6);
    }
    breadcrumbLabel.setBounds (header);

    bounds.removeFromTop (4);
    viewport.setBounds (bounds);
    rowList.setSize (juce::jmax (0, bounds.getWidth() - viewport.getScrollBarThickness()),
                     (int) rows.size() * RowList::kRowHeight);
}

void ConduitTargetPicker::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::panel);
}

//==============================================================================
void ConduitTargetPicker::RowList::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    for (const auto& row : owner.rows)
    {
        auto rowBounds = area.removeFromTop (kRowHeight);

        if (row.kind == ConduitTargetPicker::Row::Kind::header)
        {
            g.setColour (push::colours::textDim);
            g.setFont (push::scaledFont (12.0f));
            g.drawText (row.label.toUpperCase(), rowBounds.reduced (8, 0),
                        juce::Justification::bottomLeft, false);
            continue;
        }

        g.setColour (push::colours::tile);
        g.fillRoundedRectangle (rowBounds.reduced (0, 2).toFloat(), 4.0f);

        g.setColour (push::colours::text);
        g.setFont (push::scaledFont (15.0f));
        g.drawText (row.label, rowBounds.reduced (10, 0),
                    juce::Justification::centredLeft, false);

        if (row.kind == ConduitTargetPicker::Row::Kind::module)
        {
            g.setColour (push::colours::textDim);
            g.drawText (juce::String::fromUTF8 ("\xe2\x80\xba"),   // ›
                        rowBounds.reduced (10, 0), juce::Justification::centredRight, false);
        }
    }
}

void ConduitTargetPicker::RowList::mouseUp (const juce::MouseEvent& event)
{
    if (! getLocalBounds().contains (event.getPosition()))
        return;

    const auto rowIndex = event.getPosition().y / kRowHeight;
    if (rowIndex >= 0 && rowIndex < (int) owner.rows.size())
        owner.rowTapped (owner.rows[(size_t) rowIndex]);
}

} // namespace conduit
