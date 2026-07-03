#include "PushLookAndFeel.h"

#include "BinaryData.h"

namespace conduit::push
{

PushLookAndFeel::PushLookAndFeel()
{
    jostRegular = juce::Typeface::createSystemTypefaceFor (BinaryData::JostRegular_ttf,
                                                           (size_t) BinaryData::JostRegular_ttfSize);
    jostMedium  = juce::Typeface::createSystemTypefaceFor (BinaryData::JostMedium_ttf,
                                                           (size_t) BinaryData::JostMedium_ttfSize);
    jassert (jostRegular != nullptr && jostMedium != nullptr);

    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::DocumentWindow::textColourId,        colours::text);

    setColour (juce::TextButton::buttonColourId,   colours::tile);
    setColour (juce::TextButton::buttonOnColourId, colours::tileActive);
    setColour (juce::TextButton::textColourOffId,  colours::text);
    setColour (juce::TextButton::textColourOnId,   colours::ledWhite);

    setColour (juce::ComboBox::backgroundColourId, colours::tile);
    setColour (juce::ComboBox::textColourId,       colours::text);
    setColour (juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::ComboBox::arrowColourId,      colours::textDim);

    setColour (juce::PopupMenu::backgroundColourId,            colours::panel);
    setColour (juce::PopupMenu::textColourId,                  colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::tileActive);
    setColour (juce::PopupMenu::highlightedTextColourId,       colours::ledWhite);

    setColour (juce::Label::textColourId,       colours::text);
    setColour (juce::TextEditor::textColourId,  colours::text);
    setColour (juce::TextEditor::backgroundColourId, colours::tile);
    setColour (juce::TextEditor::outlineColourId,    colours::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::textDim);

    setColour (juce::Slider::backgroundColourId, colours::tile);
    setColour (juce::Slider::trackColourId,      colours::outline);
    setColour (juce::Slider::thumbColourId,      colours::ledWhite);

    setColour (juce::ToggleButton::textColourId, colours::text);
    setColour (juce::ToggleButton::tickColourId, colours::ledWhite);
}

juce::Typeface::Ptr PushLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    if (jostRegular != nullptr)
        return font.isBold() ? jostMedium : jostRegular;

    return juce::LookAndFeel_V4::getTypefaceForFont (font);
}

void PushLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                            const juce::Colour& backgroundColour,
                                            bool isHighlighted, bool isDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    constexpr auto cornerRadius = 4.0f;

    auto fill = backgroundColour;

    if (isDown)
        fill = colours::tileActive.brighter (0.1f);
    else if (isHighlighted)
        fill = colours::tileActive;

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, cornerRadius);
}

void PushLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float minSliderPos, float maxSliderPos,
                                        juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto bounds  = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto enabled = slider.isEnabled();
    const auto track   = juce::Colour (0xff1b1e22);
    const auto fill    = (enabled ? colours::textDim : colours::outline).withAlpha (0.9f);
    const auto thumb   = enabled ? colours::ledWhite : colours::textDim;

    if (style == juce::Slider::LinearVertical)
    {
        // Track: schmale Rinne mittig; Füllung von unten bis zum Griff
        const auto trackRect = bounds.withSizeKeepingCentre (4.0f, bounds.getHeight());
        g.setColour (track);
        g.fillRoundedRectangle (trackRect, 2.0f);

        g.setColour (fill);
        g.fillRoundedRectangle (trackRect.withTop (sliderPos), 2.0f);

        // Griffstein (Ableton-Fader): breites flaches Rechteck mit Mittellinie
        const auto grip = juce::Rectangle<float> (bounds.getCentreX() - 10.0f,
                                                  sliderPos - 5.0f, 20.0f, 10.0f);
        g.setColour (thumb);
        g.fillRoundedRectangle (grip, 2.0f);
        g.setColour (track);
        g.fillRect (grip.withSizeKeepingCentre (grip.getWidth() - 6.0f, 1.5f));
        return;
    }

    const auto trackRect = bounds.withSizeKeepingCentre (bounds.getWidth(), 4.0f);
    g.setColour (track);
    g.fillRoundedRectangle (trackRect, 2.0f);

    g.setColour (fill);
    g.fillRoundedRectangle (trackRect.withRight (sliderPos), 2.0f);

    const auto grip = juce::Rectangle<float> (sliderPos - 5.0f,
                                              bounds.getCentreY() - 10.0f, 10.0f, 20.0f);
    g.setColour (thumb);
    g.fillRoundedRectangle (grip, 2.0f);
    g.setColour (track);
    g.fillRect (grip.withSizeKeepingCentre (1.5f, grip.getHeight() - 6.0f));
}

void PushLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPosProportional, float rotaryStartAngle,
                                        float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds  = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto radius  = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre  = bounds.getCentre();
    const auto enabled = slider.isEnabled();
    const auto angle   = rotaryStartAngle
                       + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    // Knob-Körper
    g.setColour (enabled ? colours::tileActive : colours::tile);
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (colours::outline);
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    // Wert-Bogen: bipolar (min < 0 < max) ab der Mittelstellung — der
    // Attenuverter zeigt Richtung UND Tiefe; unipolar ab dem Anfang
    const auto range = slider.getRange();
    const auto zeroProportion = range.getStart() < 0.0 && range.getEnd() > 0.0
        ? (float) ((0.0 - range.getStart()) / range.getLength())
        : 0.0f;
    const auto zeroAngle = rotaryStartAngle
                         + zeroProportion * (rotaryEndAngle - rotaryStartAngle);

    if (std::abs (angle - zeroAngle) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius - 1.0f, radius - 1.0f, 0.0f,
                           juce::jmin (zeroAngle, angle), juce::jmax (zeroAngle, angle), true);
        g.setColour (enabled ? colours::ledWhite : colours::textDim);
        g.strokePath (arc, juce::PathStrokeType (2.0f));
    }

    // Zeiger-Linie (MI-Stil: vom Zentrum zum Rand)
    const auto tip = centre.getPointOnCircumference (radius - 2.0f, angle);
    g.setColour (enabled ? colours::text : colours::textDim);
    g.drawLine ({ centre, tip }, 1.5f);
}

juce::Font PushLookAndFeel::getJost (float height, bool medium) const
{
    const auto& typeface = medium ? jostMedium : jostRegular;

    if (typeface != nullptr)
        return juce::Font (juce::FontOptions { typeface }).withHeight (height);

    return juce::Font (juce::FontOptions {}.withHeight (height));
}

} // namespace conduit::push
