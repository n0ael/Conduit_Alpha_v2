#include "LooperOutPanel.h"

#include "PushLookAndFeel.h"

namespace conduit
{

LooperOutPanel::LooperOutPanel (juce::ValueTree nodeTreeToBind, GraphManager& graphManagerToUse)
    : nodeTree (std::move (nodeTreeToBind)),
      graphManager (graphManagerToUse),
      nodeUuid (nodeTree.getProperty (id::nodeId).toString())
{
    nodeTree.addListener (this);

    addButton.onClick = [this]
    {
        if (! frozen)
            showAddMenu();
    };
    addAndMakeVisible (addButton);

    buildRows();
}

LooperOutPanel::~LooperOutPanel()
{
    nodeTree.removeListener (this);
}

void LooperOutPanel::stopUpdates()
{
    frozen = true;
    nodeTree.removeListener (this);
    setInterceptsMouseClicks (false, false);
}

//==============================================================================
juce::ValueTree LooperOutPanel::outputsTree() const
{
    return nodeTree.getChildWithName (id::outputs);
}

void LooperOutPanel::buildRows()
{
    rows.clear();

    const auto outputs = outputsTree();
    const auto removable = outputs.getNumChildren() > 1;

    for (int i = 0; i < outputs.getNumChildren(); ++i)
    {
        const auto output = outputs.getChild (i);

        LooperOutModule::OutputSpec spec;
        spec.target = (int) output.getProperty (id::outputTarget, 0);
        spec.mode   = LooperOutModule::modeFromString (
            output.getProperty (id::outputMode).toString());
        spec.pre    = (bool) output.getProperty (id::outputPre, false);

        auto row = std::make_unique<OutputRow>();
        row->index = i;

        row->label.setText (LooperOutModule::outputLabel (spec),
                            juce::dontSendNotification);
        row->label.setColour (juce::Label::textColourId, push::colours::text);
        row->label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (row->label);

        // PRE nur für Looper-Abgriffe (der Master-Mix ist immer post-fader)
        row->preButton.setEnabled (spec.target > 0);
        row->preButton.setColour (juce::TextButton::textColourOffId,
                                  spec.pre ? push::colours::ledOrange
                                           : push::colours::textDim);
        row->preButton.onClick = [this, index = i, pre = spec.pre]
        {
            if (! frozen)
                graphManager.setLooperOutSlotPre (nodeUuid, index, ! pre);
        };
        addAndMakeVisible (row->preButton);

        row->removeButton.setEnabled (removable);
        row->removeButton.onClick = [this, index = i]
        {
            if (! frozen)
                graphManager.removeLooperOutSlot (nodeUuid, index);
        };
        addAndMakeVisible (row->removeButton);

        rows.push_back (std::move (row));
    }

    resized();
    repaint();
}

void LooperOutPanel::showAddMenu()
{
    // Ziel × Modus als Untermenüs; Item-ID = (target+1)*10 + Modus-Index
    juce::PopupMenu menu;

    const auto addModes = [] (juce::PopupMenu& target, int targetIndex)
    {
        using Mode = LooperOutModule::Mode;
        const std::pair<Mode, const char*> modes[] = {
            { Mode::stereo, "Stereo" },
            { Mode::sum,    "Summe (LR)" },
            { Mode::left,   "Nur L" },
            { Mode::right,  "Nur R" },
        };
        for (const auto& [mode, label] : modes)
            target.addItem ((targetIndex + 1) * 10 + (int) mode, label);
    };

    juce::PopupMenu master;
    addModes (master, 0);
    menu.addSubMenu ("Master", master);

    for (int looper = 1; looper <= 4; ++looper)
    {
        juce::PopupMenu sub;
        addModes (sub, looper);
        menu.addSubMenu ("Looper " + juce::String (looper), sub);
    }

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (&addButton),
                        [this, safe = juce::Component::SafePointer (this)] (int result)
    {
        if (result <= 0 || safe == nullptr || frozen)
            return;

        const auto target = result / 10 - 1;
        const auto mode = static_cast<LooperOutModule::Mode> (result % 10);
        graphManager.addLooperOutSlot (nodeUuid, target,
                                       LooperOutModule::toString (mode), false);
    });
}

//==============================================================================
void LooperOutPanel::valueTreePropertyChanged (juce::ValueTree& tree,
                                               const juce::Identifier& property)
{
    if ((property == id::outputPre || property == id::outputTarget
         || property == id::outputMode)
        && tree.hasType (id::output))
        buildRows();   // Label/PRE-Farbe folgen (auch via Undo)
}

void LooperOutPanel::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent.hasType (id::outputs))
        buildRows();
}

void LooperOutPanel::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent.hasType (id::outputs))
        buildRows();
}

//==============================================================================
juce::Rectangle<int> LooperOutPanel::rowBounds (int index) const
{
    return { 0, topPadding + index * rowHeight, getWidth(), rowHeight };
}

void LooperOutPanel::resized()
{
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        auto bounds = rowBounds (i).reduced (2);
        auto& row = *rows[(size_t) i];

        row.removeButton.setBounds (bounds.removeFromRight (24));
        row.preButton.setBounds (bounds.removeFromRight (44).reduced (2, 2));
        row.label.setBounds (bounds);
    }

    auto footer = getLocalBounds().removeFromBottom (footerHeight).reduced (2, 4);
    addButton.setBounds (footer.removeFromLeft (60).reduced (2, 0));
}

void LooperOutPanel::paint (juce::Graphics&)
{
}

} // namespace conduit
