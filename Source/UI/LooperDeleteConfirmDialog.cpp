#include "LooperDeleteConfirmDialog.h"

#include "PushLookAndFeel.h"

namespace conduit
{

LooperDeleteConfirmDialog::LooperDeleteConfirmDialog (const juce::String& title,
                                                     const juce::String& message)
{
    titleLabel.setText (title, juce::dontSendNotification);
    titleLabel.setFont (push::scaledFont (14.0f, true));
    titleLabel.setColour (juce::Label::textColourId, push::colours::text);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    messageLabel.setText (message, juce::dontSendNotification);
    messageLabel.setFont (push::scaledFont (12.0f, false));
    messageLabel.setColour (juce::Label::textColourId, push::colours::textDim);
    messageLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (messageLabel);

    const auto dismiss = [this]
    {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    };

    cancelButton.onClick = [dismiss] { dismiss(); };
    okButton.onClick = [this, dismiss]
    {
        if (onConfirm != nullptr)
            onConfirm();
        dismiss();
    };

    okButton.setColour (juce::TextButton::buttonColourId,
                        push::colours::ledRed.withAlpha (0.35f));

    addAndMakeVisible (cancelButton);
    addAndMakeVisible (okButton);

    setSize (280, 150);
}

void LooperDeleteConfirmDialog::resized()
{
    auto bounds = getLocalBounds().reduced (10);

    titleLabel.setBounds (bounds.removeFromTop (24));
    bounds.removeFromTop (4);

    auto buttons = bounds.removeFromBottom (44);
    cancelButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (4));
    okButton.setBounds (buttons.reduced (4));

    messageLabel.setBounds (bounds.reduced (0, 2));
}

} // namespace conduit
