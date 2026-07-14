#include "MappingsListComponent.h"

#include "PushLookAndFeel.h"

namespace conduit
{

//==============================================================================
MappingsListComponent::Row::Row (MappingsListComponent& ownerToUse, grid::MacroControlKey keyToUse)
    : owner (ownerToUse), key (keyToUse)
{
    nameLabel.setJustificationType (juce::Justification::centredLeft);
    nameLabel.setColour (juce::Label::textColourId, push::colours::text);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    addressLabel.setJustificationType (juce::Justification::centredLeft);
    addressLabel.setColour (juce::Label::textColourId, push::colours::textDim);
    addressLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (addressLabel);

    learnTile.onClick = [this]
    {
        if (owner.onLearnRequested != nullptr)
            owner.onLearnRequested (key);
    };
    addAndMakeVisible (learnTile);

    shiftTile.onClick = [this]
    {
        const auto* binding = owner.bindings.bindingFor (key);
        if (binding != nullptr)
            owner.bindings.setSuppressWhileShift (key, ! binding->suppressWhileShift);
        // onBindingsChanged (GridPage) stößt refresh() an -- kein lokales Update nötig.
    };
    shiftTile.setTooltip (juce::String::fromUTF8 (
        "Eigenfunktion stumm, wenn das Pad als Shift-Modifier gedient hat "
        "(feuert dann erst beim Loslassen)"));
    addChildComponent (shiftTile);   // nur bei Note-Bindungen sichtbar

    deleteTile.onClick = [this] { owner.bindings.unbind (key); };
    addAndMakeVisible (deleteTile);
}

void MappingsListComponent::Row::resized()
{
    auto bounds = getLocalBounds();

    deleteTile.setBounds (bounds.removeFromRight (kRowHeight));
    bounds.removeFromRight (kRowGap);
    shiftTile.setBounds (bounds.removeFromRight (52));
    bounds.removeFromRight (kRowGap);
    learnTile.setBounds (bounds.removeFromRight (52));
    bounds.removeFromRight (kRowGap);

    nameLabel.setBounds (bounds.removeFromTop (bounds.getHeight() / 2));
    addressLabel.setBounds (bounds);
}

void MappingsListComponent::Row::paint (juce::Graphics& g)
{
    g.setColour (push::colours::tile);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

//==============================================================================
MappingsListComponent::MappingsListComponent (grid::MidiInBindings& bindingsToUse)
    : bindings (bindingsToUse)
{
    titleLabel.setText (juce::String::fromUTF8 ("MIDI-Mappings"), juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setColour (juce::Label::textColourId, push::colours::text);
    addAndMakeVisible (titleLabel);

    hintLabel.setText (juce::String::fromUTF8 (
                           "Keine Mappings — Control antippen (Map-Overlay) oder Learn im "
                           "Macro-Panel nutzen; Pad halten + Fader bewegen lernt eine Shift-Ebene."),
                       juce::dontSendNotification);
    hintLabel.setJustificationType (juce::Justification::topLeft);
    hintLabel.setColour (juce::Label::textColourId, push::colours::textDim);
    addChildComponent (hintLabel);

    viewport.setViewedComponent (&rowHost, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    refresh();
}

MappingsListComponent::~MappingsListComponent() = default;

void MappingsListComponent::setArmedKey (bool hasArmedToUse, const grid::MacroControlKey& keyToUse)
{
    hasArmed = hasArmedToUse;
    armedKey = keyToUse;
    refresh();
}

void MappingsListComponent::refresh()
{
    rows.clear();

    for (const auto& binding : bindings.all())
    {
        auto row = std::make_unique<Row> (*this, binding.key);

        row->nameLabel.setText (controlNameFor != nullptr ? controlNameFor (binding.key)
                                                          : juce::String ("Control"),
                                juce::dontSendNotification);
        row->addressLabel.setText (describeBinding (binding), juce::dontSendNotification);
        row->learnTile.setActive (hasArmed && binding.key == armedKey);
        row->shiftTile.setVisible (binding.isNote);
        row->shiftTile.setActive (binding.suppressWhileShift);

        rowHost.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    hintLabel.setVisible (rows.empty());
    resized();
    repaint();
}

juce::String MappingsListComponent::describeBinding (const grid::MidiInBindings::Binding& binding)
{
    auto text = binding.isNote
                    ? "Note " + juce::MidiMessage::getMidiNoteName (binding.cc, true, true, 4)
                    : "CC " + juce::String (binding.cc);
    text << juce::String::fromUTF8 (" \xc2\xb7 Ch ") << juce::String (binding.channel);

    if (! binding.modifiers.empty())
    {
        juce::StringArray names;
        for (const auto& m : binding.modifiers)
            names.add (juce::MidiMessage::getMidiNoteName (m.note, true, true, 4));
        text << " + " << names.joinIntoString (", ");
    }

    return text;
}

void MappingsListComponent::resized()
{
    auto bounds = getLocalBounds().reduced (8);

    titleLabel.setBounds (bounds.removeFromTop (28));
    bounds.removeFromTop (4);
    hintLabel.setBounds (bounds);
    viewport.setBounds (bounds);

    const auto rowWidth = juce::jmax (0, bounds.getWidth() - viewport.getScrollBarThickness());
    rowHost.setSize (rowWidth, (int) rows.size() * (kRowHeight + kRowGap));

    auto rowArea = rowHost.getLocalBounds();
    for (auto& row : rows)
    {
        row->setBounds (rowArea.removeFromTop (kRowHeight));
        rowArea.removeFromTop (kRowGap);
    }
}

void MappingsListComponent::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::panel);
}

void MappingsListComponent::visibilityChanged()
{
    if (isVisible())
        refresh();   // Namen können sich geändert haben (Controls umbenannt/gelöscht)
}

} // namespace conduit
