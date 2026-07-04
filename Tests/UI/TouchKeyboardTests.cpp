#include <catch2/catch_test_macros.hpp>

#include "UI/Browser/TouchKeyboard.h"

namespace
{
struct KeyboardRig
{
    KeyboardRig()
    {
        keyboard.setSize (320, keyboard.preferredHeight());
        keyboard.setTarget (&editor);
        editor.setSize (200, 30);
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    juce::TextEditor editor;
    conduit::TouchKeyboard keyboard;
};

const juce::String backspaceLabel = juce::String::fromUTF8 ("\xe2\x8c\xab");
const juce::String closeLabel     = juce::String::fromUTF8 ("\xe2\x96\xbe");
} // namespace

//==============================================================================
TEST_CASE ("TouchKeyboard: Tasten schreiben NUR ins gebundene Suchfeld",
           "[browser][keyboard]")
{
    KeyboardRig rig;

    REQUIRE (rig.keyboard.pressKeyForTest ("t"));
    REQUIRE (rig.keyboard.pressKeyForTest ("a"));
    REQUIRE (rig.keyboard.pressKeyForTest ("p"));
    REQUIRE (rig.keyboard.pressKeyForTest ("e"));
    REQUIRE (rig.editor.getText() == "tape");

    REQUIRE (rig.keyboard.pressKeyForTest ("Leer"));
    REQUIRE (rig.editor.getText() == "tape ");

    // Backspace löscht am Caret, Clear leert komplett
    REQUIRE (rig.keyboard.pressKeyForTest (backspaceLabel));
    REQUIRE (rig.editor.getText() == "tape");
    REQUIRE (rig.keyboard.pressKeyForTest ("Clr"));
    REQUIRE (rig.editor.getText().isEmpty());

    // Ohne Ziel passiert nichts (SafePointer-Pfad, kein Crash)
    rig.keyboard.setTarget (nullptr);
    REQUIRE (rig.keyboard.pressKeyForTest ("x"));
    REQUIRE (rig.editor.getText().isEmpty());
}

TEST_CASE ("TouchKeyboard: Ziffernreihe toggelt über 123/ABC", "[browser][keyboard]")
{
    KeyboardRig rig;

    REQUIRE_FALSE (rig.keyboard.isDigitRowVisible());
    const auto lettersOnlyHeight = rig.keyboard.preferredHeight();

    // Ziffern gibt es im Buchstaben-Modus nicht
    REQUIRE_FALSE (rig.keyboard.pressKeyForTest ("7"));

    REQUIRE (rig.keyboard.pressKeyForTest ("123"));
    REQUIRE (rig.keyboard.isDigitRowVisible());
    REQUIRE (rig.keyboard.preferredHeight()
                 == lettersOnlyHeight + conduit::TouchKeyboard::keyHeight);

    REQUIRE (rig.keyboard.pressKeyForTest ("7"));
    REQUIRE (rig.editor.getText() == "7");

    REQUIRE (rig.keyboard.pressKeyForTest ("ABC"));
    REQUIRE_FALSE (rig.keyboard.isDigitRowVisible());
    REQUIRE_FALSE (rig.keyboard.pressKeyForTest ("7"));
}

TEST_CASE ("TouchKeyboard: Tasten greifen NIE den Fokus, ≥44 px hoch",
           "[browser][keyboard]")
{
    KeyboardRig rig;

    REQUIRE (conduit::TouchKeyboard::keyHeight >= 44);
    REQUIRE_FALSE (rig.keyboard.getWantsKeyboardFocus());

    int checkedKeys = 0;
    for (int i = 0; i < rig.keyboard.getNumChildComponents(); ++i)
    {
        auto* key = rig.keyboard.getChildComponent (i);
        INFO ("Taste " << key->getName());
        REQUIRE_FALSE (key->getWantsKeyboardFocus());
        REQUIRE_FALSE (key->getMouseClickGrabsKeyboardFocus());
        REQUIRE (key->getHeight() >= 40);   // keyHeight minus 2x2px-Fuge
        ++checkedKeys;
    }

    // qwertzuiop + asdfghjkl + yxcvbnm + ⌫ + 123 + Leer + Clr + ▾
    REQUIRE (checkedKeys == 31);
}

TEST_CASE ("TouchKeyboard: Schließen-Taste ruft den Besitzer", "[browser][keyboard]")
{
    KeyboardRig rig;

    int closeRequests = 0;
    rig.keyboard.onCloseRequested = [&closeRequests] { ++closeRequests; };

    REQUIRE (rig.keyboard.pressKeyForTest (closeLabel));
    REQUIRE (closeRequests == 1);
}
