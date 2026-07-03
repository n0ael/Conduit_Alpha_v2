#include "CaptureToast.h"

#include "UI/PushLookAndFeel.h"

namespace conduit
{

CaptureToast::CaptureToast()
{
    setInterceptsMouseClicks (false, false);  // klick-transparent
    setVisible (false);
}

void CaptureToast::show (const juce::String& message)
{
    text = message;
    ticksRemaining = holdTicks;
    fade = 1.0f;

    setVisible (true);
    setAlpha (1.0f);
    toFront (false);
    repaint();
    startTimer (tickIntervalMs);
}

void CaptureToast::timerCallback()
{
    if (ticksRemaining > 0)
    {
        --ticksRemaining;
        return;
    }

    fade -= 0.12f;
    if (fade <= 0.0f)
    {
        stopTimer();
        setVisible (false);
        return;
    }

    setAlpha (fade);
}

void CaptureToast::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    g.setColour (juce::Colour (0xee1c1f24));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.2f));
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    g.setColour (juce::Colours::white);
    g.setFont (push::scaledFont (15.0f));
    g.drawText (text, bounds.reduced (12.0f, 0.0f), juce::Justification::centred);
}

} // namespace conduit
