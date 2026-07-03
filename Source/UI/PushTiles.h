#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PushIcons.h"
#include "PushLookAndFeel.h"

namespace conduit::push
{

//==============================================================================
/**
    Icon-Kachel im Push-3-Stil (CLAUDE.md 10): dunkle Kachel, Vektor-Icon,
    LED-Akzent. setActive() steuert den LED-Zustand unabhängig vom
    Button-ToggleState — Status kommt von außen (Editor-Timer), Klicks
    lösen nur onClick aus. Touch-Target: die Kachel selbst (≥ 44 px im
    Header-Layout).
*/
class IconTile : public juce::Button
{
public:
    IconTile (Icon iconToUse, const juce::String& componentName,
              juce::Colour accentColour = colours::ledWhite);

    void setActive (bool shouldBeActive);
    [[nodiscard]] bool isActive() const noexcept { return active; }

    void setAccentColour (juce::Colour newAccent);

    /** Zustandsabhängige Symbole (z.B. Auge ↔ Auge-durchgestrichen in der
        Dev-Zeile des FxModulePanel). */
    void setIcon (Icon newIcon);
    [[nodiscard]] Icon getIcon() const noexcept { return icon; }

private:
    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override;

    Icon icon;
    juce::Colour accent;
    bool active = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconTile)
};

//==============================================================================
/**
    Text-Kachel im Push-3-Stil (Undo, Save, Tap, Automate, Fixed Length …).
    Optionaler Chevron ▾ rechts (Dropdown-Hinweis, z. B. Link-Menü).
    setActive() färbt den Text im Akzent (Push: beleuchtete Beschriftung).
*/
class TextTile : public juce::Button
{
public:
    explicit TextTile (const juce::String& text,
                       juce::Colour accentColour = colours::ledWhite,
                       bool showChevron = false);

    void setActive (bool shouldBeActive);
    [[nodiscard]] bool isActive() const noexcept { return active; }

    void setText (const juce::String& newText);
    [[nodiscard]] juce::String getText() const noexcept { return label; }

private:
    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override;

    juce::String label;
    juce::Colour accent;
    bool chevron;
    bool active = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TextTile)
};

//==============================================================================
/**
    Wert-Kachel wie im Ableton-Header (Tempo „120.00", Position „3. 3. 4",
    Swing „12 %"): Wert zentriert, optionale Mini-Caption darüber.

    Interaktion (alles optional — ohne Callbacks ist die Kachel reine
    Anzeige): Vertikal-Drag meldet die Gesamt-Distanz seit Drag-Start
    (der Besitzer rechnet daraus den Wert), Doppelklick öffnet einen
    Inline-Editor, Enter/Fokusverlust committet den Text.
*/
class ValueTile : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    explicit ValueTile (const juce::String& componentName);

    void setText (const juce::String& newText);          // Repaint nur bei Änderung
    [[nodiscard]] juce::String getText() const noexcept { return text; }
    void setCaption (const juce::String& newCaption);
    void setAccentColour (juce::Colour newAccent);

    std::function<void()> onDragStart;
    std::function<void (float totalDeltaY)> onDrag;      // Pixel seit Drag-Start
    std::function<void (const juce::String&)> onCommitText;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;

private:
    void showEditor();
    void commitEditor();
    void discardEditor();

    juce::String text, caption;
    juce::Colour accent = colours::text;
    std::unique_ptr<juce::TextEditor> editor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValueTile)
};

} // namespace conduit::push
