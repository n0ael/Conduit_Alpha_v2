#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace conduit::push
{

//==============================================================================
/**
    Farbwelt des Push-3-Designs (CLAUDE.md 10) — eine zentrale Definition,
    damit TransportBar, Browser und Pages identisch aussehen.

    Werte aus den Referenzfotos abgeleitet: fast-schwarze Fläche, dunkle
    Button-Kacheln mit dezenter Kontur, hellgraue Beschriftung, LED-Akzente.
*/
namespace colours
{
    inline const juce::Colour background   { 0xff121212 };  // App-Fläche
    inline const juce::Colour panel        { 0xff1a1a1a };  // Header/Panels
    inline const juce::Colour tile         { 0xff262626 };  // Button-Kachel
    inline const juce::Colour tileActive   { 0xff333333 };  // gedrückt/hover
    inline const juce::Colour outline      { 0xff3a3a3a };  // Kachel-Kontur
    inline const juce::Colour text         { 0xffd8d8d8 };
    inline const juce::Colour textDim      { 0xff8a8a8a };

    inline const juce::Colour ledGreen     { 0xff3ddc84 };  // Play
    inline const juce::Colour ledRed       { 0xffff453a };  // Automate/Looper
    inline const juce::Colour ledCyan      { 0xff00bfd8 };  // Link (Ableton-Cyan)
    inline const juce::Colour ledOrange    { 0xffffa726 };  // Capture aktiv
    inline const juce::Colour ledWhite     { 0xfff0f0f0 };  // neutrale LED
}

//==============================================================================
/**
    Globaler Schriftgrößen-Faktor (UiSettings, Dev-Panel) — Message Thread.

    setFontScale setzt NUR die Variable; die Anwendung (sendLookAndFeelChange-
    Kaskade über alle Desktop-Fenster) übernimmt der EngineEditor. Wirkung:
      - scaledFont(): direkte paint()-Textausgaben der Custom-Komponenten
      - PushLookAndFeel::getLabelFont/getTextButtonFont/getComboBoxFont/
        getPopupMenuFont: alle Standard-Widgets — Labels behalten ihre
        UNSKALIERTE Basisgröße (setFont), skaliert wird zentral beim Zeichnen.
        Deshalb nie eine bereits skalierte Font in ein Label setzen
        (Doppel-Skalierung).
*/
[[nodiscard]] float getFontScale() noexcept;
void setFontScale (float newScale) noexcept;

/** Jost in height*fontScale — zentraler Helper für paint()-Textausgaben.
    medium = Jost Medium (ersetzt "bold", via getTypefaceForFont). */
[[nodiscard]] juce::Font scaledFont (float height, bool medium = false);

//==============================================================================
/**
    Look-and-Feel im Push-3-Stil: Jost als App-Font (BinaryData, OFL-Lizenz
    in Assets/Fonts/OFL.txt), dunkle Kacheln mit 4-px-Radius, dezente
    Konturen, PopupMenus/ComboBoxen im selben Ton.

    Eine Instanz lebt im EngineEditor und wird per setDefaultLookAndFeel
    bzw. setLookAndFeel an die Header-Komponenten gereicht. Message Thread.
*/
class PushLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PushLookAndFeel();

    /** Jost Regular/Medium — Medium ersetzt "bold" (Push nutzt keine fetten
        Schnitte, nur die mittlere Stärke für Hervorhebungen). */
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& font) override;

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override;

    /** Lineare Slider im Push-Stil (Ableton-Fader-Optik): schmaler dunkler
        Track, Füllung bis zum Griff, rechteckiger Griffstein. Vertikal für
        die FX-Chassis-Fader (4.6), horizontal für die bestehenden Panels.
        Andere Styles fallen an LookAndFeel_V4 zurück. */
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override;

    /** Rotary im Mutable-Instruments-Stil (kleine Attenuverter-Knobs, 4.6):
        dunkler Körper, Zeiger-Linie, Wert-Bogen. Bipolare Ranges
        (min < 0 < max) zeichnen den Bogen ab der Mittelstellung — Standard
        für {param}_cv_amt (−1..+1, Mitte = keine Modulation). */
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override;

    /** Jost in gegebener Höhe — zentraler Helper für Custom-Komponenten.
        Multipliziert mit dem globalen fontScale. */
    [[nodiscard]] juce::Font getJost (float height, bool medium = false) const;

    //==========================================================================
    // Schriftgrößen folgen dem globalen fontScale (Dev-Panel) — Labels werden
    // beim ZEICHNEN skaliert (Basisgröße bleibt im Label), Standard-Widgets
    // über ihre LnF-Fonts. Alle Overrides skalieren das V4-Ergebnis.
    juce::Font getLabelFont (juce::Label& label) override;
    juce::Font getTextButtonFont (juce::TextButton& button, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox& box) override;
    juce::Font getPopupMenuFont() override;

    /** ToggleButtons haben in LookAndFeel_V4 eine HARTKODIERTE Schriftgröße
        (kein Font-Hook) — V4-Zeichnung nachgebaut, Font × fontScale. */
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

private:
    juce::Typeface::Ptr jostRegular;
    juce::Typeface::Ptr jostMedium;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PushLookAndFeel)
};

} // namespace conduit::push
