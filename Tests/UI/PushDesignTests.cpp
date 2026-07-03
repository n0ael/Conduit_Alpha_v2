#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "UI/PushIcons.h"
#include "UI/PushLookAndFeel.h"

using Catch::Approx;

namespace
{

constexpr conduit::push::Icon allIcons[] = {
    conduit::push::Icon::play,
    conduit::push::Icon::tapeLoop,
    conduit::push::Icon::captureFrame,
    conduit::push::Icon::metronome,
    conduit::push::Icon::plus,
    conduit::push::Icon::gear,
    conduit::push::Icon::nudgeLeft,
    conduit::push::Icon::nudgeRight,
    conduit::push::Icon::chevronDown,
    conduit::push::Icon::pageMixer,
    conduit::push::Icon::pageClip,
    conduit::push::Icon::pageDevice,
    conduit::push::Icon::pageGrid,
    conduit::push::Icon::minus,
    conduit::push::Icon::eye,
    conduit::push::Icon::eyeOff,
    conduit::push::Icon::valueButtons,
    conduit::push::Icon::fader,
    conduit::push::Icon::curve,
};

} // namespace

//==============================================================================
TEST_CASE ("PushIcons: jede Geometrie ist nicht leer und liegt in den Bounds", "[push][ui]")
{
    const juce::Rectangle<float> bounds { 10.0f, 20.0f, 64.0f, 48.0f };

    for (const auto icon : allIcons)
    {
        const auto path = conduit::push::outlinePath (icon, bounds);
        REQUIRE_FALSE (path.isEmpty());

        // Skaliert in das größte einbeschriebene Quadrat, zentriert —
        // Mittellinien dürfen die Bounds nie verlassen
        const auto pathBounds = path.getBounds();
        REQUIRE (pathBounds.getX() >= bounds.getX() - 0.01f);
        REQUIRE (pathBounds.getY() >= bounds.getY() - 0.01f);
        REQUIRE (pathBounds.getRight() <= bounds.getRight() + 0.01f);
        REQUIRE (pathBounds.getBottom() <= bounds.getBottom() + 0.01f);
    }
}

TEST_CASE ("PushIcons: Skalierung folgt den Bounds (vektorbasiert)", "[push][ui]")
{
    const auto small = conduit::push::outlinePath (conduit::push::Icon::play,
                                                   { 0.0f, 0.0f, 16.0f, 16.0f });
    const auto large = conduit::push::outlinePath (conduit::push::Icon::play,
                                                   { 0.0f, 0.0f, 160.0f, 160.0f });

    REQUIRE (large.getBounds().getWidth() > small.getBounds().getWidth() * 9.0f);
}

TEST_CASE ("PushIcons: draw() rendert jedes Icon ohne Crash sichtbar", "[push][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    for (const auto icon : allIcons)
    {
        juce::Image image (juce::Image::ARGB, 48, 48, true);

        {
            juce::Graphics g (image);
            conduit::push::draw (g, icon, { 4.0f, 4.0f, 40.0f, 40.0f },
                                 juce::Colours::white);
        }

        // Mindestens ein Pixel wurde gesetzt
        bool anyPixel = false;

        for (int y = 0; y < image.getHeight() && ! anyPixel; ++y)
            for (int x = 0; x < image.getWidth() && ! anyPixel; ++x)
                anyPixel = image.getPixelAt (x, y).getAlpha() > 0;

        REQUIRE (anyPixel);
    }
}

//==============================================================================
TEST_CASE ("PushLookAndFeel: Jost lädt aus den BinaryData-Assets", "[push][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::push::PushLookAndFeel lookAndFeel;

    const auto regular = lookAndFeel.getJost (15.0f);
    REQUIRE (regular.getTypefaceName().startsWith ("Jost"));

    const auto medium = lookAndFeel.getJost (15.0f, true);
    REQUIRE (medium.getTypefaceName().startsWith ("Jost"));

    // getTypefaceForFont: bold → Medium-Schnitt, sonst Regular
    const auto boldTypeface = lookAndFeel.getTypefaceForFont (
        juce::Font (juce::FontOptions {}.withHeight (14.0f).withStyle ("Bold")));
    REQUIRE (boldTypeface != nullptr);
    REQUIRE (boldTypeface->getName().startsWith ("Jost"));
}

TEST_CASE ("PushLookAndFeel: fontScale skaliert scaledFont, getJost und die LnF-Fonts", "[push][ui][fontscale]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::push::PushLookAndFeel lookAndFeel;

    // RAII-Guard: globaler Zustand MUSS auf 1.0 zurück (Test-Ordnung!)
    struct ScaleGuard
    {
        ~ScaleGuard() { conduit::push::setFontScale (1.0f); }
    } guard;

    REQUIRE (conduit::push::getFontScale() == Approx (1.0f));
    REQUIRE (conduit::push::scaledFont (12.0f).getHeight() == Approx (12.0f));

    conduit::push::setFontScale (1.25f);
    REQUIRE (conduit::push::scaledFont (12.0f).getHeight() == Approx (15.0f));
    REQUIRE (lookAndFeel.getJost (16.0f).getHeight() == Approx (20.0f));

    // Labels werden beim ZEICHNEN skaliert — die Basisgröße im Label bleibt
    juce::Label label;
    label.setFont (juce::Font (juce::FontOptions {}.withHeight (10.0f)));
    REQUIRE (label.getFont().getHeight() == Approx (10.0f));
    REQUIRE (lookAndFeel.getLabelFont (label).getHeight() == Approx (12.5f));

    juce::TextButton button;
    const auto unscaled = juce::LookAndFeel_V4().getTextButtonFont (button, 24).getHeight();
    REQUIRE (lookAndFeel.getTextButtonFont (button, 24).getHeight()
             == Approx (unscaled * 1.25f));
}

TEST_CASE ("PushLookAndFeel: Text rendert mit Jost ohne Crash", "[push][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::push::PushLookAndFeel lookAndFeel;

    juce::Image image (juce::Image::ARGB, 120, 30, true);

    {
        juce::Graphics g (image);
        g.setFont (lookAndFeel.getJost (16.0f));
        g.setColour (conduit::push::colours::text);
        g.drawText ("Fixed Length", image.getBounds(), juce::Justification::centred);
    }

    bool anyPixel = false;

    for (int y = 0; y < image.getHeight() && ! anyPixel; ++y)
        for (int x = 0; x < image.getWidth() && ! anyPixel; ++x)
            anyPixel = image.getPixelAt (x, y).getAlpha() > 0;

    REQUIRE (anyPixel);
}
