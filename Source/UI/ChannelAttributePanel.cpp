#include "ChannelAttributePanel.h"

#include "UI/PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr int panelWidth  = 280;
    constexpr int pad         = 16;
    constexpr int rowH        = 26;
    constexpr int swatchSize  = 24;
    constexpr int swatchGap   = 4;

    // Track-Farbpalette im Push-3-Ton (LED-Akzente + Ableton-nahe Töne).
    // Der letzte Eintrag (0) ist „keine Farbe" → Kabel nutzt die Default-Farbe.
    const std::vector<juce::uint32> paletteColours {
        0x00ff453a,  // rot
        0x00ffa726,  // orange
        0x00e8c30f,  // gelb
        0x003ddc84,  // grün
        0x0000bfd8,  // cyan
        0x004a90e2,  // blau
        0x00a066d3,  // violett
        0x00f0f0f0,  // weiß
        0          // keine
    };

    [[nodiscard]] juce::Colour opaque (juce::uint32 rgb)
    {
        return juce::Colour (0xff000000u | (rgb & 0x00ffffffu));
    }
}

//==============================================================================
ChannelAttributePanel::ChannelAttributePanel (ChannelNames& channelNamesToUse,
                                              const InputLinkSend* sendServiceToUse,
                                              int portIndexToUse,
                                              bool hasNextNeighbourToUse)
    : channelNames (channelNamesToUse),
      portIndex (portIndexToUse),
      hasNextNeighbour (hasNextNeighbourToUse)
{
    const auto dir = ChannelNames::Direction::input;

    titleLabel.setText (channelNames.getLabel (dir, portIndex), juce::dontSendNotification);
    titleLabel.setFont (push::scaledFont (15.0f, true));
    titleLabel.setColour (juce::Label::textColourId, push::colours::text);
    addAndMakeVisible (titleLabel);

    nameCaption.setText (juce::String::fromUTF8 ("Name"), juce::dontSendNotification);
    nameCaption.setFont (push::scaledFont (11.0f));
    nameCaption.setColour (juce::Label::textColourId, push::colours::textDim);
    addAndMakeVisible (nameCaption);

    nameEditor.setText (channelNames.getUserLabel (dir, portIndex), juce::dontSendNotification);
    nameEditor.setEditable (true, true, false);
    nameEditor.setColour (juce::Label::backgroundColourId, push::colours::tile);
    nameEditor.setColour (juce::Label::outlineColourId, push::colours::outline);
    nameEditor.setColour (juce::Label::textColourId, push::colours::text);
    nameEditor.onTextChange = [this]
    {
        channelNames.setUserLabel (ChannelNames::Direction::input, portIndex,
                                   nameEditor.getText());
        // Effektives Label im Titel nachziehen (Default, wenn geleert)
        titleLabel.setText (channelNames.getLabel (ChannelNames::Direction::input, portIndex),
                            juce::dontSendNotification);
    };
    addAndMakeVisible (nameEditor);

    colourCaption.setText (juce::String::fromUTF8 ("Farbe"), juce::dontSendNotification);
    colourCaption.setFont (push::scaledFont (11.0f));
    colourCaption.setColour (juce::Label::textColourId, push::colours::textDim);
    addAndMakeVisible (colourCaption);

    for (const auto rgb : paletteColours)
        swatches.push_back ({ rgb, {} });

    if (hasNextNeighbour)
    {
        stereoToggle.setButtonText (juce::String::fromUTF8 ("Mit nächstem Kanal koppeln (Stereo)"));
        stereoToggle.setToggleState (channelNames.isPortPairStart (dir, portIndex),
                                     juce::dontSendNotification);
        stereoToggle.onClick = [this]
        {
            channelNames.setPortPairedWithNext (ChannelNames::Direction::input, portIndex,
                                                stereoToggle.getToggleState());
        };
        addAndMakeVisible (stereoToggle);
    }

    sendCaption.setText (juce::String::fromUTF8 ("Link-Send"), juce::dontSendNotification);
    sendCaption.setFont (push::scaledFont (11.0f));
    sendCaption.setColour (juce::Label::textColourId, push::colours::textDim);
    addAndMakeVisible (sendCaption);

    sendButton = std::make_unique<InputSendButton> (channelNames, sendServiceToUse, portIndex);
    addAndMakeVisible (*sendButton);

    const auto stereoRows = hasNextNeighbour ? 1 : 0;
    setSize (panelWidth,
             pad + rowH                       // Titel
                 + rowH + 30                   // Name-Caption + Feld
                 + rowH + swatchSize + 6       // Farbe-Caption + Swatches
                 + stereoRows * (rowH + 4)     // Stereo-Toggle
                 + rowH                        // Send-Zeile
                 + pad);
}

ChannelAttributePanel::~ChannelAttributePanel()
{
    if (sendButton != nullptr)
        sendButton->stopUpdates();
}

//==============================================================================
juce::uint32 ChannelAttributePanel::currentColour() const
{
    return channelNames.getColour (ChannelNames::Direction::input, portIndex);
}

void ChannelAttributePanel::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::panel);

    const auto selected = currentColour();

    for (const auto& swatch : swatches)
    {
        const auto area = swatch.bounds.toFloat().reduced (1.0f);

        if (swatch.colour == 0)
        {
            // „Keine" — leerer Umriss mit diagonalem Strich
            g.setColour (push::colours::outline);
            g.drawRoundedRectangle (area, 4.0f, 1.2f);
            g.drawLine (area.getX() + 4.0f, area.getBottom() - 4.0f,
                        area.getRight() - 4.0f, area.getY() + 4.0f, 1.2f);
        }
        else
        {
            g.setColour (opaque (swatch.colour));
            g.fillRoundedRectangle (area, 4.0f);
        }

        // Auswahl-Ring
        if (swatch.colour == selected)
        {
            g.setColour (push::colours::ledWhite);
            g.drawRoundedRectangle (swatch.bounds.toFloat().reduced (0.5f), 5.0f, 2.0f);
        }
    }
}

void ChannelAttributePanel::resized()
{
    auto bounds = getLocalBounds().reduced (pad, pad);

    titleLabel.setBounds (bounds.removeFromTop (rowH));

    nameCaption.setBounds (bounds.removeFromTop (rowH).withTrimmedBottom (8));
    nameEditor.setBounds (bounds.removeFromTop (30));

    colourCaption.setBounds (bounds.removeFromTop (rowH).withTrimmedBottom (8));

    auto swatchRow = bounds.removeFromTop (swatchSize);
    int x = swatchRow.getX();
    for (auto& swatch : swatches)
    {
        swatch.bounds = { x, swatchRow.getY(), swatchSize, swatchSize };
        x += swatchSize + swatchGap;
    }
    bounds.removeFromTop (6);

    if (hasNextNeighbour)
    {
        stereoToggle.setBounds (bounds.removeFromTop (rowH));
        bounds.removeFromTop (4);
    }

    auto sendRow = bounds.removeFromTop (rowH);
    sendButton->setBounds (sendRow.removeFromLeft (28).withSizeKeepingCentre (24, 20));
    sendCaption.setBounds (sendRow.withTrimmedLeft (6));
}

void ChannelAttributePanel::mouseDown (const juce::MouseEvent& event)
{
    for (const auto& swatch : swatches)
        if (swatch.bounds.contains (event.getPosition()))
        {
            channelNames.setColour (ChannelNames::Direction::input, portIndex, swatch.colour);
            repaint();
            return;
        }
}

} // namespace conduit
