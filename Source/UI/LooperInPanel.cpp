#include "LooperInPanel.h"

#include "PushLookAndFeel.h"

namespace conduit
{

LooperInPanel::LooperInPanel (juce::ValueTree nodeTreeToBind, GraphManager& graphManagerToUse)
    : nodeTree (std::move (nodeTreeToBind)),
      graphManager (graphManagerToUse),
      nodeUuid (nodeTree.getProperty (id::nodeId).toString())
{
    nodeTree.addListener (this);

    addMonoButton.onClick = [this]
    {
        if (! frozen)
            graphManager.addLooperInSlot (nodeUuid, false);
    };
    addStereoButton.onClick = [this]
    {
        if (! frozen)
            graphManager.addLooperInSlot (nodeUuid, true);
    };

    addAndMakeVisible (addMonoButton);
    addAndMakeVisible (addStereoButton);

    buildRows();
}

LooperInPanel::~LooperInPanel()
{
    nodeTree.removeListener (this);
}

void LooperInPanel::stopUpdates()
{
    frozen = true;
    nodeTree.removeListener (this);
    setInterceptsMouseClicks (false, false);
}

//==============================================================================
juce::ValueTree LooperInPanel::inputsTree() const
{
    return nodeTree.getChildWithName (id::inputs);
}

void LooperInPanel::buildRows()
{
    rows.clear();

    const auto inputs = inputsTree();
    const auto removable = inputs.getNumChildren() > 1;

    for (int i = 0; i < inputs.getNumChildren(); ++i)
    {
        const auto input = inputs.getChild (i);

        auto row = std::make_unique<InputRow>();
        row->inputId = input.getProperty (id::inputId).toString();
        row->index = i;
        row->stereo = input.getProperty (id::inputMode).toString()
                          == LooperInModule::modeStereo;

        row->nameLabel.setEditable (false, true, false);   // Doppelklick
        row->nameLabel.setColour (juce::Label::textColourId, push::colours::text);
        row->nameLabel.onTextChange = [this, index = i]
        {
            if (frozen || ! juce::isPositiveAndBelow (index, (int) rows.size()))
                return;

            // Leerer Text = zurück zum Auto-Namen (Muster Link-Send);
            // ohne Undo — Namens-Pflege wie Slider-Werte (6.1)
            auto input = inputsTree().getChild (index);
            input.setProperty (id::inputUserName,
                               rows[(size_t) index]->nameLabel.getText().trim(), nullptr);
            refreshNameLabel (index);
        };
        addAndMakeVisible (row->nameLabel);

        row->removeButton.setEnabled (removable);
        row->removeButton.onClick = [this, index = i]
        {
            if (! frozen)
                graphManager.removeLooperInSlot (nodeUuid, index);
        };
        addAndMakeVisible (row->removeButton);

        rows.push_back (std::move (row));
        refreshNameLabel (i);
    }

    resized();
    repaint();
}

void LooperInPanel::refreshNameLabel (int rowIndex)
{
    if (! juce::isPositiveAndBelow (rowIndex, (int) rows.size()))
        return;

    const auto input = inputsTree().getChild (rowIndex);
    rows[(size_t) rowIndex]->nameLabel.setText (
        LooperInModule::effectiveInputName (input, rowIndex),
        juce::dontSendNotification);
}

//==============================================================================
void LooperInPanel::valueTreePropertyChanged (juce::ValueTree& tree,
                                              const juce::Identifier& property)
{
    if ((property == id::inputUserName || property == id::inputAutoName)
        && tree.hasType (id::input))
        refreshNameLabel (inputsTree().indexOf (tree));
}

void LooperInPanel::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent.hasType (id::inputs))
        buildRows();
}

void LooperInPanel::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent.hasType (id::inputs))
        buildRows();
}

//==============================================================================
juce::Rectangle<int> LooperInPanel::rowBounds (int index) const
{
    return { 0, topPadding + index * rowHeight, getWidth(), rowHeight };
}

void LooperInPanel::resized()
{
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        auto bounds = rowBounds (i).reduced (2);
        auto& row = *rows[(size_t) i];

        row.removeButton.setBounds (bounds.removeFromRight (24));
        bounds.removeFromRight (46);   // Mono/Stereo-Badge (paint)
        row.nameLabel.setBounds (bounds);
    }

    auto footer = getLocalBounds().removeFromBottom (footerHeight).reduced (2, 4);
    addMonoButton.setBounds (footer.removeFromLeft (footer.getWidth() / 2).reduced (2, 0));
    addStereoButton.setBounds (footer.reduced (2, 0));
}

void LooperInPanel::paint (juce::Graphics& g)
{
    g.setFont (push::scaledFont (11.0f, false));

    for (int i = 0; i < (int) rows.size(); ++i)
    {
        auto bounds = rowBounds (i).reduced (2);
        bounds.removeFromRight (24);

        g.setColour (push::colours::textDim);
        g.drawText (rows[(size_t) i]->stereo ? "STEREO" : "MONO",
                    bounds.removeFromRight (46), juce::Justification::centred, false);
    }
}

} // namespace conduit
