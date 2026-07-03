#include "FxModulePanel.h"

#include "Modules/ChassisSchema.h"

namespace conduit
{

FxModulePanel::FxModulePanel (juce::ValueTree nodeTreeToBind, GraphManager& graphManagerToUse)
    : nodeTree (std::move (nodeTreeToBind)),
      graphManager (graphManagerToUse)
{
    nodeTree.addListener (this);

    inputFader = std::make_unique<GainFaderMeter> (nodeTree, ChassisSchema::inputGainId,
                                                   graphManager, true);
    addAndMakeVisible (*inputFader);

    outputFader = std::make_unique<GainFaderMeter> (nodeTree, ChassisSchema::outputGainId,
                                                    graphManager, false);
    addAndMakeVisible (*outputFader);

    buildColumns();
}

FxModulePanel::~FxModulePanel()
{
    nodeTree.removeListener (this);
}

void FxModulePanel::stopUpdates()
{
    nodeTree.removeListener (this);
    setInterceptsMouseClicks (false, false);

    inputFader->stopUpdates();
    outputFader->stopUpdates();

    for (auto& column : columns)
    {
        column->slider.setEnabled (false);
        column->cvKnob.setEnabled (false);
    }
}

//==============================================================================
juce::ValueTree FxModulePanel::parametersTree() const
{
    return nodeTree.getChildWithName (id::parameters);
}

juce::ValueTree FxModulePanel::paramTreeFor (const juce::String& paramId) const
{
    return parametersTree().getChildWithProperty (id::paramId, paramId);
}

void FxModulePanel::buildColumns()
{
    const auto parameters = parametersTree();

    for (int i = 0; i < parameters.getNumChildren(); ++i)
    {
        const auto param = parameters.getChild (i);

        // Nur DSP-Parameter werden Spalten — Gains sind die Kanalzüge,
        // Attenuverter ziehen in M3 als Knobs unter die Fader (4.6)
        if (ChassisSchema::roleOf (param) != juce::String (ChassisSchema::roleDsp))
            continue;

        auto column = std::make_unique<ParameterColumn>();
        column->paramId    = param.getProperty (id::paramId).toString();
        column->cvAmountId = ChassisSchema::cvAmountIdFor (column->paramId);

        column->titleLabel.setText (column->paramId, juce::dontSendNotification);
        column->titleLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
        column->titleLabel.setColour (juce::Label::textColourId,
                                      juce::Colours::white.withAlpha (0.6f));
        column->titleLabel.setJustificationType (juce::Justification::centred);
        column->titleLabel.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (column->titleLabel);

        column->slider.setRange ((double) param.getProperty (id::paramMin, 0.0),
                                 (double) param.getProperty (id::paramMax, 1.0), 0.0);
        column->slider.setValue ((double) param.getProperty (id::paramValue, 0.0),
                                 juce::dontSendNotification);
        column->slider.setDoubleClickReturnValue (true, (double) param.getProperty (id::paramDefault, 0.0));

        // Schreibt NUR in den Tree — der GraphManager spiegelt aufs Atomic
        // (6.1); bewusst ohne UndoManager (Muster ParameterPanel).
        column->slider.onValueChange = [this, paramId = column->paramId, slider = &column->slider]
        {
            if (auto p = paramTreeFor (paramId); p.isValid())
                p.setProperty (id::paramValue, slider->getValue(), nullptr);
        };
        addAndMakeVisible (column->slider);

        // Attenuverter (MI-Stil): bipolar −1..+1, Mitte = keine Modulation,
        // Doppelklick zurück auf 0 — bindet {param}_cv_amt
        if (const auto cvParam = paramTreeFor (column->cvAmountId); cvParam.isValid())
        {
            column->cvKnob.setRange ((double) cvParam.getProperty (id::paramMin, -1.0),
                                     (double) cvParam.getProperty (id::paramMax, 1.0), 0.0);
            column->cvKnob.setValue ((double) cvParam.getProperty (id::paramValue, 0.0),
                                     juce::dontSendNotification);
            column->cvKnob.setDoubleClickReturnValue (true,
                (double) cvParam.getProperty (id::paramDefault, 0.0));
            column->cvKnob.onValueChange = [this, cvId = column->cvAmountId, knob = &column->cvKnob]
            {
                if (auto p = paramTreeFor (cvId); p.isValid())
                    p.setProperty (id::paramValue, knob->getValue(), nullptr);
            };
            addAndMakeVisible (column->cvKnob);
        }

        // CV-Eingangs-Port des Parameters (Kanal = firstCvChannel + Index) —
        // normale PortComponent, Kabel-Gesten laufen über den NodeCanvas
        const auto cvChannel = firstCvChannel + static_cast<int> (columns.size());
        column->cvPort = std::make_unique<PortComponent> (
            PortInfo { nodeTree.getProperty (id::nodeId).toString(), true, cvChannel, 1 });
        column->cvPort->setTooltip ("CV: " + column->paramId);
        addAndMakeVisible (*column->cvPort);

        columns.push_back (std::move (column));
    }
}

//==============================================================================
juce::Point<int> FxModulePanel::cvPortCentre (int cvChannel) const
{
    const auto index = cvChannel - firstCvChannel;

    if (index < 0 || index >= getNumColumns())
        return {};

    return columns[static_cast<size_t> (index)]->cvPort->getBounds().getCentre();
}

const PortComponent* FxModulePanel::getCvPort (int columnIndex) const noexcept
{
    if (columnIndex < 0 || columnIndex >= getNumColumns())
        return nullptr;

    return columns[static_cast<size_t> (columnIndex)]->cvPort.get();
}

//==============================================================================
void FxModulePanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    // Spalten-Fader folgen externen Quellen (OSC-Nachzug 6.1, Undo,
    // Preset-Load); die Gain-Züge hören selbst auf denselben Tree.
    if (property != id::paramValue || ! tree.hasType (id::parameter))
        return;

    const auto paramId = tree.getProperty (id::paramId).toString();

    for (auto& column : columns)
    {
        if (column->paramId == paramId)
        {
            column->slider.setValue ((double) tree.getProperty (id::paramValue),
                                     juce::dontSendNotification);
            return;
        }

        if (column->cvAmountId == paramId)
        {
            column->cvKnob.setValue ((double) tree.getProperty (id::paramValue),
                                     juce::dontSendNotification);
            return;
        }
    }
}

//==============================================================================
void FxModulePanel::resized()
{
    auto bounds = getLocalBounds();

    inputFader->setBounds (bounds.removeFromLeft (GainFaderMeter::preferredWidth));
    outputFader->setBounds (bounds.removeFromRight (GainFaderMeter::preferredWidth));

    // DSP-Spalten mittig zwischen den Gain-Zügen
    bounds.reduce (8, 0);

    for (auto& column : columns)
    {
        auto columnBounds = bounds.removeFromLeft (columnWidth);
        column->titleLabel.setBounds (columnBounds.removeFromTop (titleHeight));

        // Unten: CV-Port, darüber der Attenuverter — Rest ist der lange Fader
        auto portRow = columnBounds.removeFromBottom (portRowHeight);
        column->cvPort->setCentrePosition (portRow.getCentreX(), portRow.getCentreY());

        auto knobRow = columnBounds.removeFromBottom (knobHeight);
        column->cvKnob.setBounds (knobRow.withSizeKeepingCentre (knobHeight - 4, knobHeight - 4));

        column->slider.setBounds (columnBounds.reduced (4, 2));
    }
}

} // namespace conduit
