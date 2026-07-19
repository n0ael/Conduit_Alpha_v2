#include "LooperSendDialog.h"

#include "PushLookAndFeel.h"

namespace conduit
{

LooperSendDialog::LooperSendDialog (const juce::String& title, int sendMask, bool sendPre)
    : mask (sendMask & 0xF), pre (sendPre)
{
    titleLabel.setText (title, juce::dontSendNotification);
    titleLabel.setFont (push::scaledFont (13.0f, true));
    titleLabel.setColour (juce::Label::textColourId, push::colours::text);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    for (int s = 0; s < numSends; ++s)
    {
        sendTiles[(size_t) s]->onClick = [this, s]
        {
            mask ^= (1 << s);
            refreshTiles();
            if (onSendToggled != nullptr)
                onSendToggled (s, (mask & (1 << s)) != 0);
        };
        addAndMakeVisible (*sendTiles[(size_t) s]);
    }

    preTile.onClick = [this]
    {
        pre = ! pre;
        refreshTiles();
        if (onPreToggled != nullptr)
            onPreToggled (pre);
    };
    addAndMakeVisible (preTile);

    refreshTiles();
    setSize (228, 128);
}

void LooperSendDialog::refreshTiles()
{
    for (int s = 0; s < numSends; ++s)
        sendTiles[(size_t) s]->setActive ((mask & (1 << s)) != 0);

    preTile.setActive (pre);
    preTile.setText (pre ? "PRE" : "POST");
}

void LooperSendDialog::resized()
{
    auto bounds = getLocalBounds().reduced (8);

    titleLabel.setBounds (bounds.removeFromTop (22));
    bounds.removeFromTop (4);

    auto sendRow = bounds.removeFromTop (44);
    const auto tileWidth = sendRow.getWidth() / numSends;
    for (auto* tile : sendTiles)
        tile->setBounds (sendRow.removeFromLeft (tileWidth).reduced (2));

    bounds.removeFromTop (4);
    preTile.setBounds (bounds.removeFromTop (44).reduced (2));
}

} // namespace conduit
