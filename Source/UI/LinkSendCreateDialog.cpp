#include "LinkSendCreateDialog.h"

namespace conduit
{

namespace
{
    void styleCaption (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (13.0f)));
        label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
        label.setJustificationType (juce::Justification::centredLeft);
    }

    void styleValue (juce::Label& label)
    {
        label.setFont (juce::Font (juce::FontOptions (16.0f)));
        label.setColour (juce::Label::textColourId, juce::Colours::white);
        label.setJustificationType (juce::Justification::centred);
    }
}

//==============================================================================
LinkSendCreateDialog::LinkSendCreateDialog()
{
    styleCaption (titleLabel, juce::String::fromUTF8 ("Neuer Link-Send \xe2\x80\x94 Eing\xc3\xa4nge"));
    titleLabel.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    addAndMakeVisible (titleLabel);

    styleCaption (monoCaption,   "Mono");
    styleCaption (stereoCaption, "Stereo");
    addAndMakeVisible (monoCaption);
    addAndMakeVisible (stereoCaption);

    styleValue (monoValue);
    styleValue (stereoValue);
    addAndMakeVisible (monoValue);
    addAndMakeVisible (stereoValue);

    const auto clampCounts = [this]
    {
        monoCount   = juce::jlimit (0, maxPerType, monoCount);
        stereoCount = juce::jlimit (0, maxPerType, stereoCount);
        // mindestens ein Eingang
        if (monoCount == 0 && stereoCount == 0)
            stereoCount = 1;
        updateValueLabels();
    };

    monoMinus.onClick   = [this, clampCounts] { --monoCount;   clampCounts(); };
    monoPlus.onClick    = [this, clampCounts] { ++monoCount;   clampCounts(); };
    stereoMinus.onClick = [this, clampCounts] { --stereoCount; clampCounts(); };
    stereoPlus.onClick  = [this, clampCounts] { ++stereoCount; clampCounts(); };

    for (auto* b : { &monoMinus, &monoPlus, &stereoMinus, &stereoPlus })
        addAndMakeVisible (*b);

    createButton.onClick = [this]
    {
        if (onCreate != nullptr)
            onCreate (buildModes());

        // CallOutBox schließen (kein Modal-Loop)
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    };
    addAndMakeVisible (createButton);

    updateValueLabels();
    setSize (240, 168);
}

std::vector<LinkAudioSendModule::InputMode> LinkSendCreateDialog::buildModes() const
{
    std::vector<LinkAudioSendModule::InputMode> modes;

    for (int i = 0; i < monoCount; ++i)
        modes.push_back (LinkAudioSendModule::InputMode::mono);
    for (int i = 0; i < stereoCount; ++i)
        modes.push_back (LinkAudioSendModule::InputMode::stereo);

    if (modes.empty())
        modes.push_back (LinkAudioSendModule::InputMode::stereo);  // Sicherheitsnetz

    return modes;
}

void LinkSendCreateDialog::updateValueLabels()
{
    monoValue.setText (juce::String (monoCount),     juce::dontSendNotification);
    stereoValue.setText (juce::String (stereoCount), juce::dontSendNotification);
}

//==============================================================================
void LinkSendCreateDialog::resized()
{
    auto bounds = getLocalBounds().reduced (12);

    titleLabel.setBounds (bounds.removeFromTop (24));
    bounds.removeFromTop (6);

    const auto stepperRow = [&bounds] (juce::Label& caption, juce::Button& minus,
                                       juce::Label& value, juce::Button& plus)
    {
        auto row = bounds.removeFromTop (36);
        caption.setBounds (row.removeFromLeft (80));
        minus.setBounds (row.removeFromLeft (36));
        value.setBounds (row.removeFromLeft (44));
        plus.setBounds (row.removeFromLeft (36));
        bounds.removeFromTop (4);
    };

    stepperRow (monoCaption,   monoMinus,   monoValue,   monoPlus);
    stepperRow (stereoCaption, stereoMinus, stereoValue, stereoPlus);

    bounds.removeFromTop (4);
    createButton.setBounds (bounds.removeFromTop (36));
}

} // namespace conduit
