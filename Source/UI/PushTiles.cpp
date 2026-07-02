#include "PushTiles.h"

namespace conduit::push
{

namespace
{

void paintTileBackground (juce::Graphics& g, const juce::Rectangle<float>& bounds,
                          bool isHighlighted, bool isDown)
{
    constexpr auto cornerRadius = 4.0f;

    auto fill = colours::tile;

    if (isDown)
        fill = colours::tileActive.brighter (0.1f);
    else if (isHighlighted)
        fill = colours::tileActive;

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, cornerRadius);
}

juce::Font jostFont (const juce::Component& component, float height)
{
    // Läuft über den Default-LookAndFeel (PushLookAndFeel → Jost); der
    // Fallback ist der System-Sans — Komponente bleibt LnF-unabhängig nutzbar
    juce::ignoreUnused (component);
    return juce::Font (juce::FontOptions {}.withHeight (height));
}

} // namespace

//==============================================================================
IconTile::IconTile (Icon iconToUse, const juce::String& componentName, juce::Colour accentColour)
    : juce::Button (componentName), icon (iconToUse), accent (accentColour)
{
}

void IconTile::setActive (bool shouldBeActive)
{
    if (active == shouldBeActive)
        return;

    active = shouldBeActive;
    repaint();
}

void IconTile::setAccentColour (juce::Colour newAccent)
{
    if (accent == newAccent)
        return;

    accent = newAccent;
    repaint();
}

void IconTile::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    paintTileBackground (g, bounds, isHighlighted, isDown);

    auto colour = active ? accent : colours::text;

    if (! isEnabled())
        colour = colours::textDim.withAlpha (0.5f);

    draw (g, icon, bounds.reduced (bounds.getHeight() * 0.26f), colour);
}

//==============================================================================
TextTile::TextTile (const juce::String& text, juce::Colour accentColour, bool showChevron)
    : juce::Button (text), label (text), accent (accentColour), chevron (showChevron)
{
}

void TextTile::setActive (bool shouldBeActive)
{
    if (active == shouldBeActive)
        return;

    active = shouldBeActive;
    repaint();
}

void TextTile::setText (const juce::String& newText)
{
    if (label == newText)
        return;

    label = newText;
    repaint();
}

void TextTile::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    paintTileBackground (g, bounds, isHighlighted, isDown);

    auto colour = active ? accent : colours::text;

    if (! isEnabled())
        colour = colours::textDim.withAlpha (0.5f);

    auto textArea = bounds.reduced (6.0f, 0.0f);

    if (chevron)
    {
        const auto chevronArea = textArea.removeFromRight (12.0f);
        draw (g, Icon::chevronDown, chevronArea.withSizeKeepingCentre (10.0f, 10.0f),
              colour.withAlpha (0.8f));
    }

    g.setColour (colour);
    g.setFont (jostFont (*this, 13.0f));
    g.drawText (label, textArea, juce::Justification::centred);
}

//==============================================================================
ValueTile::ValueTile (const juce::String& componentName)
{
    setName (componentName);
}

void ValueTile::setText (const juce::String& newText)
{
    if (text == newText)
        return;

    text = newText;
    repaint();
}

void ValueTile::setCaption (const juce::String& newCaption)
{
    if (caption == newCaption)
        return;

    caption = newCaption;
    repaint();
}

void ValueTile::setAccentColour (juce::Colour newAccent)
{
    if (accent == newAccent)
        return;

    accent = newAccent;
    repaint();
}

void ValueTile::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    paintTileBackground (g, bounds, false, false);

    auto area = bounds.reduced (6.0f, 3.0f);

    if (caption.isNotEmpty())
    {
        g.setColour (colours::textDim);
        g.setFont (jostFont (*this, 9.0f));
        g.drawText (caption, area.removeFromTop (10.0f), juce::Justification::centred);
    }

    if (editor == nullptr)
    {
        g.setColour (isEnabled() ? accent : colours::textDim.withAlpha (0.5f));
        g.setFont (jostFont (*this, caption.isNotEmpty() ? 15.0f : 16.0f));
        g.drawText (text, area, juce::Justification::centred);
    }
}

void ValueTile::resized()
{
    if (editor != nullptr)
        editor->setBounds (getLocalBounds().reduced (3));
}

void ValueTile::mouseDown (const juce::MouseEvent& event)
{
    juce::ignoreUnused (event);

    if (isEnabled() && onDragStart != nullptr)
        onDragStart();
}

void ValueTile::mouseDrag (const juce::MouseEvent& event)
{
    if (isEnabled() && onDrag != nullptr)
        onDrag ((float) -event.getDistanceFromDragStartY());  // hoch = mehr
}

void ValueTile::mouseDoubleClick (const juce::MouseEvent& event)
{
    juce::ignoreUnused (event);

    if (isEnabled() && onCommitText != nullptr)
        showEditor();
}

void ValueTile::showEditor()
{
    editor = std::make_unique<juce::TextEditor>();
    editor->setJustification (juce::Justification::centred);
    editor->setFont (jostFont (*this, 15.0f));
    editor->setText (text, juce::dontSendNotification);
    editor->setSelectAllWhenFocused (true);

    editor->onReturnKey    = [this] { commitEditor(); };
    editor->onFocusLost    = [this] { commitEditor(); };
    editor->onEscapeKey    = [this] { discardEditor(); };

    addAndMakeVisible (*editor);
    resized();
    editor->grabKeyboardFocus();
    repaint();
}

void ValueTile::commitEditor()
{
    if (editor == nullptr)
        return;

    const auto entered = editor->getText();
    discardEditor();

    if (onCommitText != nullptr)
        onCommitText (entered);
}

void ValueTile::discardEditor()
{
    if (editor == nullptr)
        return;

    // Der Aufruf kommt aus einem TextEditor-Callback (onReturnKey/onFocusLost)
    // — das Objekt darf nicht in seinem eigenen Callback-Frame sterben.
    // Callbacks zuerst lösen (setVisible löst sonst onFocusLost-Re-Entry aus),
    // dann ausblenden + Destruktion auf den nächsten Message-Loop-Durchlauf.
    editor->onReturnKey = nullptr;
    editor->onFocusLost = nullptr;
    editor->onEscapeKey = nullptr;
    editor->setVisible (false);
    removeChildComponent (editor.get());

    juce::MessageManager::callAsync ([doomed = std::shared_ptr<juce::TextEditor> (editor.release())] {});

    repaint();
}

} // namespace conduit::push
