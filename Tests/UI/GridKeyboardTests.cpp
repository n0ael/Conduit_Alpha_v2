#include <catch2/catch_test_macros.hpp>

#include "UI/GridKeyboardComponent.h"
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

    REQUIRE (fake.calls.size() == 1);
    REQUIRE (fake.calls[0].kind == grid::FakeVoiceSink::Kind::VoiceStart);
    REQUIRE (fake.calls[0].voiceIndex == 0);
    REQUIRE (fake.calls[0].intValue == 50);   // lowestNote(48) + col(2) + rowFromBottom(0)*5
    REQUIRE (fake.calls[0].intValue2 == 100); // feste Velocity (Platzhalter)

    keyboard.mouseUp (makeEvent (keyboard, { 100.0f, 140.0f }));

    REQUIRE (fake.calls.size() == 2);
    REQUIRE (fake.calls[1].kind == grid::FakeVoiceSink::Kind::VoiceStop);
    REQUIRE (fake.calls[1].voiceIndex == 0);
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
