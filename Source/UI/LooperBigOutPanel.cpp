#include "LooperBigOutPanel.h"

#include "PushLookAndFeel.h"

namespace conduit
{

LooperBigOutPanel::LooperBigOutPanel (juce::ValueTree nodeTreeToBind)
    : nodeTree (std::move (nodeTreeToBind))
{
    nodeTree.addListener (this);
    rebuildSpecs();
}

LooperBigOutPanel::~LooperBigOutPanel()
{
    nodeTree.removeListener (this);
}

void LooperBigOutPanel::stopUpdates()
{
    frozen = true;
    nodeTree.removeListener (this);
    setInterceptsMouseClicks (false, false);
}

void LooperBigOutPanel::rebuildSpecs()
{
    specs = LooperBigOutModule::readOutputConfig (nodeTree);
    repaint();
}

//==============================================================================
void LooperBigOutPanel::valueTreePropertyChanged (juce::ValueTree& tree,
                                                  const juce::Identifier&)
{
    if (tree.hasType (id::output))
        rebuildSpecs();
}

void LooperBigOutPanel::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent.hasType (id::outputs) || parent == nodeTree)
        rebuildSpecs();
}

void LooperBigOutPanel::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent.hasType (id::outputs) || parent == nodeTree)
        rebuildSpecs();
}

//==============================================================================
void LooperBigOutPanel::paint (juce::Graphics& g)
{
    using Kind = LooperBigOutModule::Kind;

    g.setFont (push::scaledFont (12.0f, false));

    for (int i = 0; i < (int) specs.size(); ++i)
    {
        const auto& spec = specs[(size_t) i];
        const juce::Rectangle<int> row { 0, topPadding + i * rowHeight,
                                         getWidth(), rowHeight };

        // Sektions-Trenner beim Kind-Wechsel (Tracks | Busse | Sends | Master)
        if (i > 0 && spec.kind != specs[(size_t) (i - 1)].kind)
        {
            g.setColour (push::colours::outline);
            g.fillRect (row.getX() + 4, row.getY(), row.getWidth() - 8, 1);
        }

        g.setColour (spec.kind == Kind::master ? push::colours::text
                                               : push::colours::textDim);
        g.drawText (LooperBigOutModule::outputLabel (spec),
                    row.reduced (8, 0), juce::Justification::centredLeft, false);
    }
}

} // namespace conduit
