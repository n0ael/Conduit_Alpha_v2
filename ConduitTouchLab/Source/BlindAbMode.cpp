#include "BlindAbMode.h"

namespace touchlab
{

BlindAbMode::BlindAbMode()
{
    // Nicht-Button-Fläche lässt Drags zur NativeTouchSource darunter durch;
    // die Buttons (Kinder) fangen ihre Klicks weiterhin.
    setInterceptsMouseClicks (false, true);

    for (auto* b : { &selAButton, &selBButton, &verdictAButton, &verdictBButton, &resolveButton })
        addAndMakeVisible (b);

    selAButton.setClickingTogglesState (false);
    selBButton.setClickingTogglesState (false);

    selAButton.onClick = [this] { selected = 'A'; repaint(); };
    selBButton.onClick = [this] { selected = 'B'; repaint(); };
    verdictAButton.onClick = [this] { castVerdict ('A'); };
    verdictBButton.onClick = [this] { castVerdict ('B'); };
    resolveButton.onClick = [this] { revealed = true; repaint(); };

    resolveButton.setButtonText (juce::String::fromUTF8 ("Aufl\xc3\xb6sen"));

    newRound();
    setActive (false);
}

void BlindAbMode::setActive (bool shouldBeActive)
{
    active = shouldBeActive;
    setVisible (active);
    if (active)
    {
        revealed = false;
        newRound();
    }
    repaint();
}

SourceTag BlindAbMode::channelSource (char channel) const
{
    const bool wantNative = (channel == 'A') ? aIsNative : ! aIsNative;
    return wantNative ? SourceTag::Native : SourceTag::RawPointer;
}

void BlindAbMode::newRound()
{
    aIsNative = rng.nextBool();
    selected = 'A';
    hasDot = false;
    repaint();
}

void BlindAbMode::castVerdict (char finerChannel)
{
    const auto src = channelSource (finerChannel);
    if (src == SourceTag::Native) ++nativeWins; else ++rawWins;
    ++trials;

    lastResult = juce::String ("Runde ") + juce::String (trials) + ": Kanal "
               + juce::String::charToString ((juce::juce_wchar) finerChannel)
               + " = " + (src == SourceTag::Native ? "Nativ" : "Raw-Pointer");

    newRound();
}

void BlindAbMode::consume (SourceTag tag, juce::Point<float> filtered, Phase phase)
{
    if (! active)
        return;

    if (tag == channelSource (selected))
    {
        dot = filtered;
        hasDot = (phase != Phase::Up);
        repaint();
    }
}

//==============================================================================
void BlindAbMode::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto buttons = area.removeFromBottom (72);

    auto rowSel = buttons.removeFromTop (32);
    selAButton.setBounds (rowSel.removeFromLeft (rowSel.getWidth() / 2).reduced (2));
    selBButton.setBounds (rowSel.reduced (2));

    buttons.removeFromTop (4);
    auto rowVerdict = buttons.removeFromTop (32);
    const int third = rowVerdict.getWidth() / 3;
    verdictAButton.setBounds (rowVerdict.removeFromLeft (third).reduced (2));
    verdictBButton.setBounds (rowVerdict.removeFromLeft (third).reduced (2));
    resolveButton.setBounds  (rowVerdict.reduced (2));
}

void BlindAbMode::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().reduced (10);
    g.setColour (juce::Colour (0xff1a1c1f));
    g.fillRoundedRectangle (area.toFloat(), 4.0f);

    auto stage = area.reduced (6);
    stage.removeFromBottom (72 + 8);

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (14.0f);
    g.drawText (juce::String::fromUTF8 ("Blindvergleich — aktiver Kanal: ")
                    + juce::String::charToString ((juce::juce_wchar) selected),
                stage.removeFromTop (20), juce::Justification::topLeft);

    // Indikator-Punkt (was Leon fühlt)
    if (hasDot)
    {
        const auto p = dot;
        g.setColour (juce::Colour (0xffe0e0e0));
        g.fillEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f, 1.0f);
    }

    // Auszählung
    auto footer = area.removeFromBottom (72).removeFromTop (0); // Platzhalter
    juce::ignoreUnused (footer);

    g.setFont (12.0f);
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    juce::String tally = "Durchgänge: " + juce::String (trials);
    if (revealed)
        tally << "   |   Nativ: " << nativeWins << "   Raw-Pointer: " << rawWins;
    else
        tally << juce::String::fromUTF8 ("   |   (verdeckt — „Auflösen“ für Auszählung)");

    g.drawText (tally, stage.removeFromBottom (18), juce::Justification::bottomLeft);

    if (lastResult.isNotEmpty() && revealed)
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawText (lastResult, stage.removeFromBottom (16), juce::Justification::bottomLeft);
    }
}

} // namespace touchlab
