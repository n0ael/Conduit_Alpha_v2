#include "LooperPage.h"

#include "Core/Looper/LooperMath.h"
#include "PushLookAndFeel.h"

namespace conduit
{

LooperPage::LooperPage()
{
    sourceCaption.setText ("Source", juce::dontSendNotification);
    sourceCaption.setColour (juce::Label::textColourId, push::colours::textDim);
    sourceCaption.setFont (push::scaledFont (13.0f));
    addAndMakeVisible (sourceCaption);

    sourceCombo.setTextWhenNothingSelected ("Quelle wählen …");
    sourceCombo.onChange = [this]
    {
        const auto index = sourceCombo.getSelectedItemIndex();
        if (onSourceSelected != nullptr
            && juce::isPositiveAndBelow (index, (int) currentSources.size()))
            onSourceSelected (currentSources[(size_t) index].key);
    };
    addAndMakeVisible (sourceCombo);

    // Stop kommt mit dem Playback (Baustein B5) — sichtbar, aber disabled
    stopTile.setEnabled (false);
    stopTile.setTooltip (juce::String::fromUTF8 ("Loop-Playback — Baustein B5"));
    addAndMakeVisible (stopTile);

    statusLabel.setColour (juce::Label::textColourId, push::colours::textDim);
    statusLabel.setFont (push::scaledFont (15.0f));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);
}

//==============================================================================
void LooperPage::setSources (std::vector<Source> sources, const juce::String& selectedKey)
{
    currentSources = std::move (sources);

    sourceCombo.clear (juce::dontSendNotification);

    int selectedItemId = 0;
    for (size_t i = 0; i < currentSources.size(); ++i)
    {
        const auto itemId = (int) i + 1;  // ComboBox-Ids sind 1-basiert
        sourceCombo.addItem (currentSources[i].label, itemId);

        if (currentSources[i].key == selectedKey)
            selectedItemId = itemId;
    }

    // Unbekannter/leerer Schlüssel → erste Quelle (Master) als Anzeige,
    // bewusst OHNE Notification: die Persistenz ändert nur der User-Klick
    if (selectedItemId == 0 && ! currentSources.empty())
        selectedItemId = 1;

    if (selectedItemId > 0)
        sourceCombo.setSelectedId (selectedItemId, juce::dontSendNotification);
}

void LooperPage::setStatus (const juce::String& statusText)
{
    if (statusLabel.getText() != statusText)
        statusLabel.setText (statusText, juce::dontSendNotification);
}

//==============================================================================
void LooperPage::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::background);

    // Platzhalter des Waveform-Strips (B4): Kachel-Fläche + die vier
    // Segment-Beschriftungen des Endlesss-Layouts
    if (! stripArea.isEmpty())
    {
        const auto strip = stripArea.toFloat();
        g.setColour (push::colours::tile);
        g.fillRoundedRectangle (strip, 6.0f);

        const auto segmentWidth = strip.getWidth() / (float) looper::numSegments;
        g.setColour (push::colours::textDim.withAlpha (0.4f));
        for (int i = 1; i < looper::numSegments; ++i)
            g.drawVerticalLine ((int) (strip.getX() + segmentWidth * (float) i),
                                strip.getY() + 8.0f, strip.getBottom() - 28.0f);

        g.setColour (push::colours::textDim);
        g.setFont (push::scaledFont (15.0f));
        for (int i = 0; i < looper::numSegments; ++i)
        {
            const auto bars = looper::barsForSegment (i);
            const auto label = juce::String (bars) + (bars == 1 ? " Bar" : " Bars");
            g.drawText (label,
                        juce::Rectangle<float> (strip.getX() + segmentWidth * (float) i,
                                                strip.getBottom() - 26.0f,
                                                segmentWidth, 20.0f),
                        juce::Justification::centred);
        }

        g.setFont (push::scaledFont (14.0f));
        g.drawText (juce::String::fromUTF8 ("Wellenform — Baustein B4"),
                    strip.withTrimmedBottom (28.0f),
                    juce::Justification::centred);
    }
}

void LooperPage::resized()
{
    auto bounds = getLocalBounds().reduced (16);

    // Kopfzeile: Source-Caption + Selektor + Stop (Touch-Ziele ≥ 44 px)
    auto header = bounds.removeFromTop (44);
    sourceCaption.setBounds (header.removeFromLeft (64));
    sourceCombo.setBounds (header.removeFromLeft (280).reduced (0, 4));
    header.removeFromLeft (12);
    stopTile.setBounds (header.removeFromLeft (88));

    bounds.removeFromTop (12);

    // Waveform-Strip: volle Breite, großzügige Höhe (Segment-Klick = Commit)
    stripArea = bounds.removeFromTop (juce::jmax (120, bounds.getHeight() / 2));

    bounds.removeFromTop (8);
    statusLabel.setBounds (bounds.removeFromTop (24));
}

} // namespace conduit
