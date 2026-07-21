#include "ExpressionRibbon.h"

#include "PushLookAndFeel.h"

namespace conduit
{

ExpressionRibbon::ExpressionRibbon (juce::String labelText, bool bipolarMode)
    : label (std::move (labelText)), bipolar (bipolarMode),
      currentValue (bipolarMode ? 0.5f : 0.0f), // bipolar startet neutral (Mitte), nicht am unteren Anschlag
      fillColour (push::colours::ledWhite)
{
    setWantsKeyboardFocus (false);
    setInterceptsMouseClicks (true, false);
}

void ExpressionRibbon::setFillColour (juce::Colour newColour)
{
    if (fillColour == newColour)
        return;

    fillColour = newColour;
    repaint();
}

float ExpressionRibbon::valueForNormY (float normY) noexcept
{
    return juce::jlimit (0.0f, 1.0f, 1.0f - normY);
}

void ExpressionRibbon::handlePointer (const juce::MouseEvent& event)
{
    const auto bounds = getLocalBounds().toFloat();
    if (bounds.getHeight() <= 0.0f)
        return;

    currentValue = valueForNormY (event.position.y / bounds.getHeight());

    if (onValueChanged)
        onValueChanged (currentValue);

    repaint();
}

void ExpressionRibbon::mouseDown (const juce::MouseEvent& event) { handlePointer (event); }

void ExpressionRibbon::mouseDrag (const juce::MouseEvent& event)
{
    // Wert folgt der absoluten Y-Position → NoCursor (Füllstand zeigt den
    // Wert), erst beim tatsächlichen Ziehen.
    cursorHider.begin (*this, event, ui::DragCursorHider::Mode::absolute);
    handlePointer (event);
}

void ExpressionRibbon::mouseUp (const juce::MouseEvent&) { cursorHider.end(); }

void ExpressionRibbon::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    g.setColour (push::colours::tile);
    g.fillRoundedRectangle (bounds, 4.0f);

    const auto fillY = bounds.getY() + bounds.getHeight() * (1.0f - currentValue);

    if (bipolar)
    {
        const auto middleY = bounds.getY() + bounds.getHeight() * 0.5f;
        const auto top     = juce::jmin (fillY, middleY);
        const auto bottom  = juce::jmax (fillY, middleY);

        g.setColour (fillColour);
        g.fillRoundedRectangle ({ bounds.getX(), top, bounds.getWidth(), bottom - top }, 4.0f);

        g.setColour (push::colours::outline);
        g.drawLine (bounds.getX(), middleY, bounds.getRight(), middleY, 1.5f);
    }
    else
    {
        g.setColour (fillColour);
        g.fillRoundedRectangle (bounds.withTop (fillY), 4.0f);
    }

    g.setColour (push::colours::outline);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    g.setColour (push::colours::textDim);
    g.setFont (push::scaledFont (12.0f));   // Design-Mock Grid-Page v2: Jost 12
    g.drawFittedText (label, getLocalBounds().removeFromBottom (20), juce::Justification::centred, 1, 1.0f);
}

} // namespace conduit
