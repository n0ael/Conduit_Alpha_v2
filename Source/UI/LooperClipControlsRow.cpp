#include "LooperClipControlsRow.h"

namespace conduit
{

//==============================================================================
LooperClipControlsRow::LooperClipControlsRow()
{
    setName ("looperClipControls");

    reverseTile.onClick = [this] { if (controlsEnabled && onReverseToggled) onReverseToggled(); };

    // Sync (Takt-Raster) / Free (stufenlos) — Modus der LEN/POS-Potis
    syncFreeTile.setActive (true);
    syncFreeTile.onClick = [this]
    {
        if (onSyncFreeToggled != nullptr)
            onSyncFreeToggled (! syncMode);
    };

    // LEN: Loop-Länge (Sync: /8 /4 /2 /1 · Free: 50 ms–60 s); DK = voll
    lenKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    lenKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    lenKnob.setRange (0.0, 1.0, 0.0);
    lenKnob.setValue (1.0, juce::dontSendNotification);
    lenKnob.setDoubleClickReturnValue (true, 1.0);
    lenKnob.setName ("lenKnob");
    lenKnob.onValueChange = [this]
    {
        if (! controlsEnabled)
            return;
        if (onLoopLenChanged != nullptr)
            onLoopLenChanged (lenKnob.getValue(), syncMode);
    };

    // POS: Loop-Fenster verschieben (Sync: fensterweise · Free: ms); DK = 0
    posKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    posKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    posKnob.setRange (0.0, 1.0, 0.0);
    posKnob.setValue (0.0, juce::dontSendNotification);
    posKnob.setDoubleClickReturnValue (true, 0.0);
    posKnob.setName ("posKnob");
    posKnob.onValueChange = [this]
    {
        if (! controlsEnabled)
            return;
        if (onLoopPosChanged != nullptr)
            onLoopPosChanged (posKnob.getValue(), syncMode);
    };

    lenLabel.setJustificationType (juce::Justification::centred);
    lenLabel.setInterceptsMouseClicks (false, false);
    lenLabel.setColour (juce::Label::textColourId, push::colours::textDim);
    posLabel.setJustificationType (juce::Justification::centred);
    posLabel.setInterceptsMouseClicks (false, false);
    posLabel.setColour (juce::Label::textColourId, push::colours::textDim);

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

    addAndMakeVisible (syncFreeTile);
    addAndMakeVisible (lenKnob);
    addAndMakeVisible (lenLabel);
    addAndMakeVisible (posKnob);
    addAndMakeVisible (posLabel);
    addAndMakeVisible (reverseTile);
    addAndMakeVisible (variKnob);
    addAndMakeVisible (rateLabel);
    addAndMakeVisible (rasterTile);
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
    // VARI Display (07/2026): der Editor injiziert den Formatter
    // („+3 st" oder „♭3" bei Quant); Fallback = Faktor-Anzeige
    const auto text = rateFormatter != nullptr
                    ? rateFormatter (currentRate)
                    : juce::String (currentRate, 2) + juce::String::fromUTF8 ("×");
    rateLabel.setText (text, juce::dontSendNotification);
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
    syncFreeTile.setAlpha (alpha);
    lenKnob.setAlpha (alpha);
    posKnob.setAlpha (alpha);
    reverseTile.setAlpha (alpha);
    variKnob.setAlpha (alpha);
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
    rasterTile.setText (quantized ? "Quant" : "Tape");
    updateRateLabel();
}

void LooperClipControlsRow::setSyncFree (bool sync)
{
    if (syncMode == sync)
        return;
    syncMode = sync;
    syncFreeTile.setActive (sync);
    syncFreeTile.setText (sync ? "Sync" : "Free");
}

void LooperClipControlsRow::setLoopLenNorm (double norm01, const juce::String& display)
{
    if (! lenKnob.isMouseButtonDown())
        lenKnob.setValue (juce::jlimit (0.0, 1.0, norm01), juce::dontSendNotification);
    lenLabel.setText (display, juce::dontSendNotification);
}

void LooperClipControlsRow::setLoopPosNorm (double norm01, const juce::String& display)
{
    if (! posKnob.isMouseButtonDown())
        posKnob.setValue (juce::jlimit (0.0, 1.0, norm01), juce::dontSendNotification);
    posLabel.setText (display, juce::dontSendNotification);
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
    const auto knobWidth = juce::jmax (tileWidth, 44);

    syncFreeTile.setBounds (bounds.removeFromLeft (juce::jmax (tileWidth, 48)).reduced (1));

    auto lenArea = bounds.removeFromLeft (knobWidth);
    lenLabel.setBounds (lenArea.removeFromBottom (14));
    lenKnob.setBounds (lenArea);

    auto posArea = bounds.removeFromLeft (knobWidth);
    posLabel.setBounds (posArea.removeFromBottom (14));
    posKnob.setBounds (posArea);

    reverseTile.setBounds (bounds.removeFromLeft (tileWidth).reduced (1));

    auto knobArea = bounds.removeFromLeft (knobWidth);
    rateLabel.setBounds (knobArea.removeFromBottom (14));
    variKnob.setBounds (knobArea);

    rasterTile.setBounds (bounds.removeFromLeft (juce::jmax (tileWidth, 52)).reduced (1));

    if (targetTile.isVisible())
        targetTile.setBounds (bounds.removeFromLeft (juce::jmax (tileWidth, 48)).reduced (1));

    activeLabel.setBounds (bounds.reduced (2, 0));
}

void LooperClipControlsRow::paint (juce::Graphics& g)
{
    g.setColour (push::colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
}

} // namespace conduit
