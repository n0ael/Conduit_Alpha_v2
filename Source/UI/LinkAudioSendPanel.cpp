#include "LinkAudioSendPanel.h"

#include "Modules/ConduitModule.h"
#include "UI/PushLookAndFeel.h"

namespace conduit
{

namespace
{
    juce::Colour colourFor (LinkAudioSendModule::SendStatus status)
    {
        using S = LinkAudioSendModule::SendStatus;
        return status == S::streaming ? juce::Colour (0xff58d68d)
             : status == S::announced ? juce::Colour (0xffe8b339)
                                       : juce::Colour (0xff5a6170);
    }

    constexpr int maxNameLength = 32;
}

//==============================================================================
LinkAudioSendPanel::LinkAudioSendPanel (juce::ValueTree nodeTreeToBind, GraphManager& graphManagerToUse)
    : nodeTree (std::move (nodeTreeToBind)),
      graphManager (graphManagerToUse),
      nodeUuid (nodeTree.getProperty (id::nodeId).toString())
{
    nodeTree.addListener (this);

    refreshButton.setTooltip (juce::String::fromUTF8 ("Kanal-Namen aus den angeschlossenen Quellen \xc3\xbc" "bernehmen"));
    refreshButton.onClick = [this] { graphManager.refreshAutoNames (nodeUuid); };
    addAndMakeVisible (refreshButton);

    buildRows();

    startTimerHz (10);  // Statuswechsel sind selten
}

LinkAudioSendPanel::~LinkAudioSendPanel()
{
    nodeTree.removeListener (this);
}

void LinkAudioSendPanel::stopUpdates()
{
    stopTimer();
    nodeTree.removeListener (this);
    setInterceptsMouseClicks (false, false);
    refreshButton.setEnabled (false);

    for (auto& row : rows)
    {
        row->nameLabel.setEnabled (false);
        row->gainSlider.setEnabled (false);
    }
}

//==============================================================================
juce::ValueTree LinkAudioSendPanel::inputsTree() const
{
    return nodeTree.getChildWithName (id::inputs);
}

juce::ValueTree LinkAudioSendPanel::gainParamFor (const juce::String& paramId) const
{
    return nodeTree.getChildWithName (id::parameters)
               .getChildWithProperty (id::paramId, paramId);
}

void LinkAudioSendPanel::buildRows()
{
    const auto inputs = inputsTree();

    for (int i = 0; i < inputs.getNumChildren(); ++i)
    {
        const auto in = inputs.getChild (i);

        auto row = std::make_unique<InputRow>();
        row->index       = i;
        row->inputId     = in.getProperty (id::inputId).toString();
        row->gainParamId = in.getProperty (id::inputGainParamId).toString();

        // Name-Editor: Doppelklick editiert; zeigt den effektiven Namen
        row->nameLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
        row->nameLabel.setEditable (false, true, false);
        row->nameLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        row->nameLabel.onTextChange = [this, index = i]
        {
            writeUserName (index, rows[static_cast<std::size_t> (index)]->nameLabel.getText());
        };
        addAndMakeVisible (row->nameLabel);

        // Attenuator: schreibt paramValue in den Tree (GraphManager spiegelt aufs Atomic)
        const auto param = gainParamFor (row->gainParamId);
        row->gainSlider.setRange ((double) param.getProperty (id::paramMin, 0.0),
                                  (double) param.getProperty (id::paramMax, 1.0), 0.0);
        row->gainSlider.setValue ((double) param.getProperty (id::paramValue, 1.0),
                                  juce::dontSendNotification);
        row->gainSlider.setDoubleClickReturnValue (true, (double) param.getProperty (id::paramDefault, 1.0));
        row->gainSlider.onValueChange = [this, paramId = row->gainParamId, slider = &row->gainSlider]
        {
            if (auto p = gainParamFor (paramId); p.isValid())
                p.setProperty (id::paramValue, slider->getValue(), nullptr);
        };
        addAndMakeVisible (row->gainSlider);

        rows.push_back (std::move (row));
        refreshNameLabel (i);
    }
}

//==============================================================================
void LinkAudioSendPanel::writeUserName (int rowIndex, const juce::String& text)
{
    const auto inputs = inputsTree();
    if (rowIndex < 0 || rowIndex >= inputs.getNumChildren())
        return;

    // userName leer → zurück zum Auto-Namen. Freiform-Name (Anzeige), aber
    // getrimmt und gedeckelt — landet als {moduleId}/{name} im Link-Kanal.
    const auto trimmed = text.trim().substring (0, maxNameLength);
    inputs.getChild (rowIndex).setProperty (id::inputUserName, trimmed, nullptr);
    refreshNameLabel (rowIndex);
}

void LinkAudioSendPanel::refreshNameLabel (int rowIndex)
{
    const auto inputs = inputsTree();
    if (rowIndex < 0 || rowIndex >= inputs.getNumChildren()
        || rowIndex >= static_cast<int> (rows.size()))
        return;

    const auto in = inputs.getChild (rowIndex);
    const auto hasUserName = in.getProperty (id::inputUserName).toString().isNotEmpty();

    auto& label = rows[static_cast<std::size_t> (rowIndex)]->nameLabel;
    label.setText (LinkAudioSendModule::effectiveInputName (in, rowIndex), juce::dontSendNotification);
    // Auto-/Default-Name dezenter als ein gesetzter User-Name
    label.setColour (juce::Label::textColourId,
                     juce::Colours::white.withAlpha (hasUserName ? 0.9f : 0.5f));
}

//==============================================================================
void LinkAudioSendPanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    // Namen folgen externen Quellen (Auto-Naming-Snapshot, Refresh, Undo)
    if ((property == id::inputUserName || property == id::inputAutoName)
        && tree.hasType (id::input))
    {
        refreshNameLabel (inputsTree().indexOf (tree));
        return;
    }

    // Attenuator-Slider folgt OSC-Nachzug/Undo/Preset — dontSendNotification
    if (property == id::paramValue && tree.hasType (id::parameter))
    {
        const auto paramId = tree.getProperty (id::paramId).toString();
        for (auto& row : rows)
            if (row->gainParamId == paramId)
            {
                row->gainSlider.setValue ((double) tree.getProperty (id::paramValue),
                                          juce::dontSendNotification);
                return;
            }
    }
}

//==============================================================================
void LinkAudioSendPanel::timerCallback()
{
    refreshNow();
}

void LinkAudioSendPanel::refreshNow()
{
    // Transiente Auflösung pro Tick (5.3) — nullptr während Deleting/Swap
    auto* module = dynamic_cast<LinkAudioSendModule*> (graphManager.getModuleFor (nodeUuid));

    bool changed = false;
    for (auto& row : rows)
    {
        const auto status = module != nullptr ? module->getSlotStatusForUi (row->index)
                                              : LinkAudioSendModule::SendStatus::offline;
        if (status != row->status)
        {
            row->status = status;
            changed = true;
        }
    }

    if (changed)
        repaint();
}

//==============================================================================
juce::Rectangle<int> LinkAudioSendPanel::rowBounds (int index) const
{
    return { 0, topPadding + index * rowHeight, getWidth(), rowHeight };
}

void LinkAudioSendPanel::resized()
{
    for (int i = 0; i < static_cast<int> (rows.size()); ++i)
    {
        auto area = rowBounds (i).reduced (0, 3);
        area.removeFromLeft (16);                       // Platz für die Status-LED (gemalt)
        rows[static_cast<std::size_t> (i)]->nameLabel.setBounds (area.removeFromLeft (78));
        area.removeFromLeft (20);                       // Platz für den Mono/Stereo-Badge (gemalt)
        rows[static_cast<std::size_t> (i)]->gainSlider.setBounds (area);
    }

    auto footer = getLocalBounds().removeFromBottom (footerHeight).reduced (0, 4);
    refreshButton.setBounds (footer.removeFromLeft (juce::jmin (120, footer.getWidth())));
}

void LinkAudioSendPanel::paint (juce::Graphics& g)
{
    const auto inputs = inputsTree();

    for (int i = 0; i < static_cast<int> (rows.size()); ++i)
    {
        const auto area = rowBounds (i);

        // Status-LED ganz links
        g.setColour (colourFor (rows[static_cast<std::size_t> (i)]->status));
        g.fillEllipse (juce::Rectangle<float> (3.0f, static_cast<float> (area.getCentreY() - 4), 8.0f, 8.0f));

        // Mono/Stereo-Badge zwischen Name und Slider
        const auto stereo = inputs.getChild (i).getProperty (id::inputMode).toString()
                            == LinkAudioSendModule::modeStereo;
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (push::scaledFont (11.0f));
        g.drawText (stereo ? "S" : "M",
                    juce::Rectangle<int> (94, area.getY(), 18, area.getHeight()),
                    juce::Justification::centred);
    }
}

} // namespace conduit
