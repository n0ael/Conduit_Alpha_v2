#include "LooperClipControlsRow.h"

namespace conduit
{

//==============================================================================
LooperClipControlsRow::LooperClipControlsRow()
{
    setName ("looperClipControls");

    doubleTile.onClick = [this] { if (controlsEnabled && onDoubleLength) onDoubleLength(); };
    halveTile.onClick  = [this] { if (controlsEnabled && onHalveLength) onHalveLength(); };
    reverseTile.onClick = [this] { if (controlsEnabled && onReverseToggled) onReverseToggled(); };
    syncTile.onClick   = [this] { if (controlsEnabled && onResetWithSync) onResetWithSync(); };

    rasterTile.onClick = [this]
    {
        if (onRasterToggled != nullptr)
            onRasterToggled (! rasterQuantized);
    };

    targetTile.onShortClick = [this] { if (onTargetCycle) onTargetCycle(); };
    targetTile.onHoldChanged = [this] (bool holding)
    {
        targetTile.setActive (holding);
        if (onTargetHold != nullptr)
            onTargetHold (holding);
    };

    // VARI: Rotary in Oktaven (−2..+2), Doppelklick = 1× (Reset),
    // Rastung via snapFunction/Detent im onValueChange-Pfad
    variKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    variKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    variKnob.setRange (-looperui::variOctaveRange, looperui::variOctaveRange, 0.0);
    variKnob.setValue (0.0, juce::dontSendNotification);
    variKnob.setDoubleClickReturnValue (true, 0.0);
    variKnob.setName ("variKnob");
    variKnob.onValueChange = [this] { knobMoved(); };

    rateLabel.setJustificationType (juce::Justification::centred);
    rateLabel.setInterceptsMouseClicks (false, false);
    rateLabel.setColour (juce::Label::textColourId, push::colours::textDim);

    activeLabel.setJustificationType (juce::Justification::centredRight);
    activeLabel.setInterceptsMouseClicks (false, false);
    activeLabel.setColour (juce::Label::textColourId, push::colours::textDim);

    addAndMakeVisible (doubleTile);
    addAndMakeVisible (halveTile);
    addAndMakeVisible (reverseTile);
    addAndMakeVisible (variKnob);
    addAndMakeVisible (rateLabel);
    addAndMakeVisible (rasterTile);
    addAndMakeVisible (syncTile);
    addAndMakeVisible (targetTile);
    addAndMakeVisible (activeLabel);

    setClipControlsEnabled (false);
    setActiveLabel ({});
    updateRateLabel();
}

void LooperClipControlsRow::knobMoved()
{
    if (! controlsEnabled)
    {
        // Ohne Aktiv-Clip wirkungslos: Knob zurück auf den Anzeige-Wert
        variKnob.setValue (looperui::octavesFromRate (currentRate),
                           juce::dontSendNotification);
        return;
    }

    auto octaves = variKnob.getValue();
    octaves = (rasterQuantized && snapFunction != nullptr)
            ? snapFunction (octaves)
            : looperui::applyDetent (octaves);

    const auto rate = looperui::rateFromOctaves (octaves);
    if (juce::exactlyEqual (rate, currentRate))
        return;

    currentRate = rate;
    updateRateLabel();

    if (onRateChanged != nullptr)
        onRateChanged (rate);
}

void LooperClipControlsRow::updateRateLabel()
{
    rateLabel.setText (juce::String (currentRate, 2) + juce::String::fromUTF8 ("×"),
                       juce::dontSendNotification);
    rateLabel.setColour (juce::Label::textColourId,
                         std::abs (currentRate - 1.0) > 1.0e-3
                             ? push::colours::ledOrange
                             : push::colours::textDim);
}

//==============================================================================
void LooperClipControlsRow::setClipControlsEnabled (bool enabled)
{
    controlsEnabled = enabled;

    const auto alpha = enabled ? 1.0f : 0.4f;
    doubleTile.setAlpha (alpha);
    halveTile.setAlpha (alpha);
    reverseTile.setAlpha (alpha);
    variKnob.setAlpha (alpha);
    syncTile.setAlpha (alpha);
}

void LooperClipControlsRow::setRate (double rate)
{
    currentRate = rate;
    variKnob.setValue (looperui::octavesFromRate (rate), juce::dontSendNotification);
    updateRateLabel();
}

void LooperClipControlsRow::setReversed (bool reversed)
{
    reverseTile.setActive (reversed);
}

void LooperClipControlsRow::setRasterQuantized (bool quantized)
{
    rasterQuantized = quantized;
    rasterTile.setActive (quantized);
}

void LooperClipControlsRow::setTargetVisible (bool visible)
{
    targetTile.setVisible (visible);
}

void LooperClipControlsRow::setActiveLabel (const juce::String& label)
{
    activeLabel.setText (label.isNotEmpty() ? label
                                            : juce::String ("kein Clip aktiv"),
                         juce::dontSendNotification);
}

//==============================================================================
void LooperClipControlsRow::resized()
{
    auto bounds = getLocalBounds().reduced (2);
    const auto tileWidth = juce::jmin (44, bounds.getWidth() / 8);

    doubleTile.setBounds (bounds.removeFromLeft (tileWidth).reduced (1));
    halveTile.setBounds (bounds.removeFromLeft (tileWidth).reduced (1));
    reverseTile.setBounds (bounds.removeFromLeft (tileWidth).reduced (1));

    auto knobArea = bounds.removeFromLeft (juce::jmax (tileWidth, 48));
    rateLabel.setBounds (knobArea.removeFromBottom (14));
    variKnob.setBounds (knobArea);

    rasterTile.setBounds (bounds.removeFromLeft (tileWidth).reduced (1));
    syncTile.setBounds (bounds.removeFromLeft (tileWidth).reduced (1));

    if (targetTile.isVisible())
        targetTile.setBounds (bounds.removeFromLeft (juce::jmax (tileWidth, 64)).reduced (1));

    activeLabel.setBounds (bounds.reduced (2, 0));
}

void LooperClipControlsRow::paint (juce::Graphics& g)
{
    g.setColour (push::colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
}

} // namespace conduit
