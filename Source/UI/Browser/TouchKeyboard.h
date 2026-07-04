#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace conduit
{

//==============================================================================
/**
    Kompakte On-Screen-Tastatur für das Browser-Suchfeld (M5) —
    smartphone-typisches QWERTZ-Minimallayout:

        [1 2 3 4 5 6 7 8 9 0]   (umschaltbar über die 123/ABC-Taste)
        [q w e r t z u i o p]
        [ a s d f g h j k l ]
        [ y x c v b n m  ⌫  ]
        [123] [  Leer  ] [Clr] [▾]

    Zeichen gehen AUSSCHLIESSLICH an den gebundenen TextEditor
    (SafePointer, insertTextAtCaret/deleteBackwards/setText) — keine
    globalen Key-Injections. Alle Tasten sind ≥ 44 px hoch (Prompt-Regel;
    die Breite teilt die Panel-Spalte) und greifen NIE den Keyboard-Fokus
    (setWantsKeyboardFocus(false) + setMouseClickGrabsKeyboardFocus(false))
    — sonst schlösse der Fokusverlust die Tastatur beim ersten Tippen.

    Sichtbarkeit/Animation verwaltet der Besitzer (BrowserPanel).
    Nur Message Thread.
*/
class TouchKeyboard final : public juce::Component
{
public:
    TouchKeyboard();

    /** Ziel-Editor (SafePointer — Ziel darf jederzeit sterben). */
    void setTarget (juce::TextEditor* editor);

    /** Ziffernreihe ein-/ausklappen (123/ABC-Taste ruft das selbst). */
    void setDigitRowVisible (bool shouldShow);
    [[nodiscard]] bool isDigitRowVisible() const noexcept { return digitRowVisible; }

    /** ▾-Taste: der Besitzer klappt die Tastatur ein. */
    std::function<void()> onCloseRequested;

    /** Gesamthöhe für das Panel-Layout (hängt an der Ziffernreihe). */
    [[nodiscard]] int preferredHeight() const;

    static constexpr int keyHeight = 46;   // ≥ 44 px (Touch-Regel)

    void resized() override;
    void paint (juce::Graphics& g) override;

    /** Test-Seam: Taste mit diesem Label drücken (derselbe Pfad wie
        der Touch-Tap); false wenn es die Taste nicht gibt. */
    bool pressKeyForTest (const juce::String& label);

private:
    //==========================================================================
    /** Eine Taste — zeichnet sich im Push-Stil, greift nie den Fokus. */
    class Key final : public juce::Button
    {
    public:
        explicit Key (const juce::String& labelToUse);

        std::function<void()> onPressed;

    private:
        void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Key)
    };

    void rebuildKeys();
    void handleKey (const juce::String& label);

    [[nodiscard]] juce::TextEditor* target() const { return targetEditor.getComponent(); }

    juce::Component::SafePointer<juce::TextEditor> targetEditor;
    juce::OwnedArray<Key> keys;
    bool digitRowVisible = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchKeyboard)
};

} // namespace conduit
