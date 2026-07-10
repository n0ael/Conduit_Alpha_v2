#include <catch2/catch_test_macros.hpp>

#include "UI/GridKeyboardComponent.h"
#include "UI/GridPage.h"
#include "UI/PushLookAndFeel.h"
#include "Core/GridVoiceEngine.h"
#include "../Core/FakeVoiceSink.h"

namespace grid = conduit::grid;

namespace
{
    juce::MouseEvent makeEvent (juce::Component& eventComponent, juce::Point<float> position)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), position,
                 juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 &eventComponent, &eventComponent, now, position, now, 1, false };
    }
} // namespace

//==============================================================================
TEST_CASE ("GridKeyboardComponent: Tap in ein Pad startet die erwartete Note", "[grid][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);
    conduit::GridKeyboardComponent keyboard (engine);
    keyboard.setSize (320, 160);   // 8x4 Raster -> 40x40 px pro Pad (Default-Config)

    // Pad (col=2, row=3 -- unterste Reihe): Mitte bei (100, 140)
    keyboard.mouseDown (makeEvent (keyboard, { 100.0f, 140.0f }));

    // noteOn + expliziter Startwert für Bend/Pressure (sonst läse das MPE-
    // Instrument den Alt-Zustand des zuletzt auf diesem Kanal genutzten
    // Voice-Slots statt 0/Ist-Position, Fund 06.07.2026)
    REQUIRE (fake.calls.size() == 3);
    REQUIRE (fake.calls[0].kind == grid::FakeVoiceSink::Kind::VoiceStart);
    REQUIRE (fake.calls[0].voiceIndex == 0);
    REQUIRE (fake.calls[0].intValue == 50);   // lowestNote(48) + col(2) + rowFromBottom(0)*5
    REQUIRE (fake.calls[0].intValue2 == 100); // feste Velocity (Platzhalter)
    REQUIRE (fake.calls[1].kind == grid::FakeVoiceSink::Kind::PitchBend);
    REQUIRE (juce::exactlyEqual (fake.calls[1].floatValue, 0.0f));
    REQUIRE (fake.calls[2].kind == grid::FakeVoiceSink::Kind::Pressure);
    REQUIRE (juce::exactlyEqual (fake.calls[2].floatValue, 0.5f)); // Touch exakt mittig im Pad

    keyboard.mouseUp (makeEvent (keyboard, { 100.0f, 140.0f }));

    REQUIRE (fake.calls.size() == 4);
    REQUIRE (fake.calls[3].kind == grid::FakeVoiceSink::Kind::VoiceStop);
    REQUIRE (fake.calls[3].voiceIndex == 0);
}

TEST_CASE ("GridKeyboardComponent: Tap außerhalb des Rasters bleibt wirkungslos", "[grid][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);
    conduit::GridKeyboardComponent keyboard (engine);
    keyboard.setSize (320, 160);

    // padIndexAt clamped auf -1 kann bei gültigen Component-Bounds nicht
    // vorkommen -- Drag ohne vorherigen Down darf trotzdem nichts auslösen.
    keyboard.mouseDrag (makeEvent (keyboard, { 100.0f, 140.0f }));
    REQUIRE (fake.calls.empty());
}

//==============================================================================
TEST_CASE ("GridKeyboardComponent: padBaseColour folgt der Session-Skala", "[grid][ui]")
{
    using conduit::GridKeyboardComponent;
    using conduit::ScaleType;
    namespace colours = conduit::push::colours;

    // C-Dur, Root C (0): Grundton #383838, Skalenton Kachelgrau, skalenfremd unbeleuchtet
    REQUIRE (GridKeyboardComponent::padBaseColour (48, 0, ScaleType::major) == colours::padRoot);   // C
    REQUIRE (GridKeyboardComponent::padBaseColour (50, 0, ScaleType::major) == colours::tile);      // D
    REQUIRE (GridKeyboardComponent::padBaseColour (49, 0, ScaleType::major) == colours::padUnlit);  // C#

    // Oktav-Wrap: H unterhalb des Root-C ist die grosse Septime von C-Dur
    REQUIRE (GridKeyboardComponent::padBaseColour (47, 0, ScaleType::major) == colours::tile);

    // Root-Versatz D (2): D-Pads sind Grundton, C# wird grosse Septime von D-Dur
    REQUIRE (GridKeyboardComponent::padBaseColour (50, 2, ScaleType::major) == colours::padRoot);
    REQUIRE (GridKeyboardComponent::padBaseColour (49, 2, ScaleType::major) == colours::tile);

    // Chromatisch: alles Skalenton -- nur der Grundton bekommt den Akzent
    REQUIRE (GridKeyboardComponent::padBaseColour (49, 0, ScaleType::chromatic) == colours::tile);
    REQUIRE (GridKeyboardComponent::padBaseColour (60, 0, ScaleType::chromatic) == colours::padRoot);

    // Pentatonik-Gegenprobe: Dur-Pentatonik (0 2 4 7 9) kennt keine Quarte
    REQUIRE (GridKeyboardComponent::padBaseColour (53, 0, ScaleType::pentatonic) == colours::padUnlit);
}

TEST_CASE ("GridPage: Skala-Kacheln zykeln Root und Typ wie im Mock", "[grid][ui]")
{
    using conduit::GridPage;
    using conduit::ScaleType;

    // Root-Kachel: C -> C# -> ... -> B -> C (12er-Zyklus)
    REQUIRE (GridPage::nextScaleRoot (0) == 1);
    REQUIRE (GridPage::nextScaleRoot (10) == 11);
    REQUIRE (GridPage::nextScaleRoot (11) == 0);

    REQUIRE (GridPage::noteNameFor (0) == "C");
    REQUIRE (GridPage::noteNameFor (1) == "C#");
    REQUIRE (GridPage::noteNameFor (11) == "B");

    // Skala-Kachel: chromatic -> major -> minor -> pentatonic -> chromatic
    REQUIRE (GridPage::nextScaleType (ScaleType::chromatic)  == ScaleType::major);
    REQUIRE (GridPage::nextScaleType (ScaleType::major)      == ScaleType::minor);
    REQUIRE (GridPage::nextScaleType (ScaleType::minor)      == ScaleType::pentatonic);
    REQUIRE (GridPage::nextScaleType (ScaleType::pentatonic) == ScaleType::chromatic);

    // Anzeigenamen mit grossem Anfangsbuchstaben
    REQUIRE (GridPage::scaleDisplayNameFor (ScaleType::chromatic)  == "Chromatic");
    REQUIRE (GridPage::scaleDisplayNameFor (ScaleType::major)      == "Major");
    REQUIRE (GridPage::scaleDisplayNameFor (ScaleType::minor)      == "Minor");
    REQUIRE (GridPage::scaleDisplayNameFor (ScaleType::pentatonic) == "Pentatonic");
}
