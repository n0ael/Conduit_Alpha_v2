#include "TouchKeyboard.h"

#include "UI/PushLookAndFeel.h"

namespace conduit
{

namespace
{
    const juce::String kBackspace = juce::String::fromUTF8 ("\xe2\x8c\xab");   // ⌫
    const juce::String kClose     = juce::String::fromUTF8 ("\xe2\x96\xbe");   // ▾
    const juce::String kDigits    = "123";
    const juce::String kLetters   = "ABC";
    const juce::String kSpace     = "Leer";
    const juce::String kClear     = "Clr";

    const juce::StringArray kDigitRow  { "1","2","3","4","5","6","7","8","9","0" };
    const juce::StringArray kRowTop    { "q","w","e","r","t","z","u","i","o","p" };
    const juce::StringArray kRowMiddle { "a","s","d","f","g","h","j","k","l" };
    const juce::StringArray kRowBottom { "y","x","c","v","b","n","m" };
} // namespace

//==============================================================================
TouchKeyboard::Key::Key (const juce::String& labelToUse)
    : juce::Button (labelToUse)
{
    // NIE den Fokus greifen — sonst schließt der Fokusverlust des
    // Suchfelds die Tastatur beim ersten Tastendruck (Kopf-Doku)
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);

    onClick = [this] { if (onPressed != nullptr) onPressed(); };
}

void TouchKeyboard::Key::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    g.setColour (isDown ? push::colours::tileActive
                        : isHighlighted ? push::colours::tile.brighter (0.06f)
                                        : push::colours::tile);
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (push::colours::text);
    g.setFont (push::scaledFont (getName().length() > 1 ? 13.0f : 16.0f));
    // Nie horizontal stauchen (User-Regel 07/2026)
    g.drawText (getName(), getLocalBounds(), juce::Justification::centred, true);
}

//==============================================================================
TouchKeyboard::TouchKeyboard()
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    rebuildKeys();
}

void TouchKeyboard::setTarget (juce::TextEditor* editor)
{
    targetEditor = editor;
}

void TouchKeyboard::setDigitRowVisible (bool shouldShow)
{
    if (digitRowVisible == shouldShow)
        return;

    digitRowVisible = shouldShow;
    rebuildKeys();

    // Höhe ändert sich — der Besitzer layoutet über preferredHeight()
    if (auto* parent = getParentComponent())
        parent->resized();
}

int TouchKeyboard::preferredHeight() const
{
    return keyHeight * (digitRowVisible ? 5 : 4);
}

//==============================================================================
void TouchKeyboard::rebuildKeys()
{
    keys.clear();

    const auto addKey = [this] (const juce::String& label)
    {
        auto* key = keys.add (new Key (label));
        key->onPressed = [this, label] { handleKey (label); };
        addAndMakeVisible (key);
    };

    if (digitRowVisible)
        for (const auto& digit : kDigitRow)
            addKey (digit);

    for (const auto& row : { kRowTop, kRowMiddle, kRowBottom })
        for (const auto& letter : row)
            addKey (letter);

    addKey (kBackspace);
    addKey (digitRowVisible ? kLetters : kDigits);
    addKey (kSpace);
    addKey (kClear);
    addKey (kClose);

    resized();
}

void TouchKeyboard::resized()
{
    auto bounds = getLocalBounds();
    const auto width = (float) bounds.getWidth();
    int keyIndex = 0;

    const auto layoutRow = [&] (int count, juce::Rectangle<int> rowArea)
    {
        const auto keyWidth = (float) rowArea.getWidth() / (float) count;
        for (int i = 0; i < count; ++i)
        {
            if (keyIndex >= keys.size())
                return;

            keys[keyIndex++]->setBounds (
                juce::Rectangle<float> ((float) rowArea.getX() + keyWidth * (float) i,
                                        (float) rowArea.getY(), keyWidth,
                                        (float) rowArea.getHeight()).toNearestInt());
        }
    };

    if (digitRowVisible)
        layoutRow (10, bounds.removeFromTop (keyHeight));

    layoutRow (10, bounds.removeFromTop (keyHeight));                       // qwertzuiop

    auto middle = bounds.removeFromTop (keyHeight);                          // asdfghjkl
    middle.removeFromLeft ((int) (width * 0.05f));
    middle.removeFromRight ((int) (width * 0.05f));
    layoutRow (9, middle);

    layoutRow (8, bounds.removeFromTop (keyHeight));                        // yxcvbnm + ⌫

    // Unterste Reihe: [123|ABC] [   Leer   ] [Clr] [▾]
    auto bottom = bounds.removeFromTop (keyHeight);
    if (keyIndex < keys.size())
        keys[keyIndex++]->setBounds (bottom.removeFromLeft ((int) (width * 0.16f)));
    if (keyIndex < keys.size())
        keys[keyIndex++]->setBounds (bottom.removeFromLeft ((int) (width * 0.50f)));
    if (keyIndex < keys.size())
        keys[keyIndex++]->setBounds (bottom.removeFromLeft ((int) (width * 0.17f)));
    if (keyIndex < keys.size())
        keys[keyIndex++]->setBounds (bottom);
}

void TouchKeyboard::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::panel);

    g.setColour (push::colours::outline);
    g.fillRect (0, 0, getWidth(), 1);
}

//==============================================================================
void TouchKeyboard::handleKey (const juce::String& label)
{
    // Modus-/Schließ-Tasten brauchen kein Ziel
    if (label == kDigits)  { setDigitRowVisible (true);  return; }
    if (label == kLetters) { setDigitRowVisible (false); return; }

    if (label == kClose)
    {
        if (onCloseRequested != nullptr)
            onCloseRequested();
        return;
    }

    auto* editor = target();
    if (editor == nullptr)
        return;

    if (label == kBackspace)
    {
        editor->deleteBackwards (false);
        return;
    }

    if (label == kClear)
    {
        editor->setText ({}, juce::sendNotification);
        return;
    }

    editor->insertTextAtCaret (label == kSpace ? juce::String (" ") : label);
}

bool TouchKeyboard::pressKeyForTest (const juce::String& label)
{
    for (auto* key : keys)
    {
        if (key->getName() == label)
        {
            handleKey (label);
            return true;
        }
    }

    return false;
}

} // namespace conduit
