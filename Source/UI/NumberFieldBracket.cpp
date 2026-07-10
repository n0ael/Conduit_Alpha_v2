#include "NumberFieldBracket.h"

namespace conduit
{

NumberFieldBracket::NumberFieldBracket() : NumberFieldBracket (Config{})
{
}

NumberFieldBracket::NumberFieldBracket (const Config& cfg)
    : config (cfg), value (clampAndStep (cfg.defaultValue, cfg))
{
    setInterceptsMouseClicks (true, false);
}

double NumberFieldBracket::clampAndStep (double raw, const Config& cfg) noexcept
{
    auto clamped = juce::jlimit (juce::jmin (cfg.minValue, cfg.maxValue),
                                 juce::jmax (cfg.minValue, cfg.maxValue), raw);

    if (cfg.step > 0.0)
    {
        clamped = cfg.minValue + std::round ((clamped - cfg.minValue) / cfg.step) * cfg.step;
        clamped = juce::jlimit (juce::jmin (cfg.minValue, cfg.maxValue),
                                juce::jmax (cfg.minValue, cfg.maxValue), clamped);
    }

    return clamped;
}

double NumberFieldBracket::valueFromDrag (double valueAtDragStart, float dragDeltaYPx,
                                          const Config& cfg) noexcept
{
    // Swipe nach OBEN (dragDeltaYPx negativ) erhöht den Wert.
    const auto raw = valueAtDragStart + (double) (-dragDeltaYPx) * cfg.unitsPerPixel;
    return clampAndStep (raw, cfg);
}

void NumberFieldBracket::setValue (double newValue, juce::NotificationType notification)
{
    const auto clamped = clampAndStep (newValue, config);

    // Bewusst exakter Vergleich (No-Op-Guard) -- juce::exactlyEqual statt
    // ==, Clang -Wfloat-equal.
    if (juce::exactlyEqual (clamped, value))
        return;

    value = clamped;
    repaint();

    if (notification != juce::dontSendNotification)
        if (onValueChanged)
            onValueChanged (value);
}

void NumberFieldBracket::setAccentColour (juce::Colour newColour) noexcept
{
    if (accentColour == newColour)
        return;

    accentColour = newColour;
    repaint();
}

void NumberFieldBracket::mouseDown (const juce::MouseEvent& event)
{
    if (event.getNumberOfClicks() >= 2)
    {
        setValue (clampAndStep (config.defaultValue, config));
        if (onValueCommitted)
            onValueCommitted (value);
        return;
    }

    touched = true;
    valueAtDragStart = value;
    repaint();
}

void NumberFieldBracket::mouseDrag (const juce::MouseEvent& event)
{
    setValue (valueFromDrag (valueAtDragStart, (float) event.getDistanceFromDragStartY(), config));
}

void NumberFieldBracket::mouseUp (const juce::MouseEvent&)
{
    if (! touched)
        return;

    touched = false;
    repaint();

    if (onValueCommitted)
        onValueCommitted (value);
}

void NumberFieldBracket::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    auto textArea = bounds;

    if (config.label.isNotEmpty())
    {
        auto labelArea = textArea.removeFromLeft (textArea.getWidth() * 0.4f);
        g.setColour (push::colours::textDim);
        g.setFont (push::scaledFont (12.0f));
        g.drawFittedText (config.label, labelArea.toNearestInt(), juce::Justification::centredLeft,
                          1, 1.0f);
    }

    const auto valueText = juce::String (value, config.decimals);
    g.setColour (touched ? accentColour : push::colours::text);
    g.setFont (push::scaledFont (14.0f));
    g.drawFittedText (valueText, textArea.toNearestInt(), juce::Justification::centred, 1, 1.0f);

    if (touched)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (push::scaledFont (14.0f), valueText, 0.0f, 0.0f);
        const auto valueTextWidth = glyphs.getBoundingBox (0, -1, true).getWidth();
        const auto bracketArea = textArea.withSizeKeepingCentre (
            juce::jmin (textArea.getWidth(), valueTextWidth + 24.0f), textArea.getHeight());
        const auto thickness = juce::jmax (2.0f, (float) getHeight() * kBorderFactor);
        const auto bracketWidth = 6.0f;

        g.setColour (accentColour);

        // Linke Klammer.
        juce::Path left;
        left.startNewSubPath (bracketArea.getX() + bracketWidth, bracketArea.getY());
        left.lineTo (bracketArea.getX(), bracketArea.getY());
        left.lineTo (bracketArea.getX(), bracketArea.getBottom());
        left.lineTo (bracketArea.getX() + bracketWidth, bracketArea.getBottom());
        g.strokePath (left, juce::PathStrokeType (thickness));

        // Rechte Klammer.
        juce::Path right;
        right.startNewSubPath (bracketArea.getRight() - bracketWidth, bracketArea.getY());
        right.lineTo (bracketArea.getRight(), bracketArea.getY());
        right.lineTo (bracketArea.getRight(), bracketArea.getBottom());
        right.lineTo (bracketArea.getRight() - bracketWidth, bracketArea.getBottom());
        g.strokePath (right, juce::PathStrokeType (thickness));
    }
}

} // namespace conduit
