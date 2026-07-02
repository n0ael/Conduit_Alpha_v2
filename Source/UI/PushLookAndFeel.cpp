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

juce::Font PushLookAndFeel::getJost (float height, bool medium) const
{
    const auto& typeface = medium ? jostMedium : jostRegular;

    if (typeface != nullptr)
        return juce::Font (juce::FontOptions { typeface }).withHeight (height);

    return juce::Font (juce::FontOptions {}.withHeight (height));
}

} // namespace conduit::push
