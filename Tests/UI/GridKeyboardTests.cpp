#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "UI/GridKeyboardComponent.h"
#include "UI/GridPage.h"
#include "UI/PushLookAndFeel.h"
#include "Core/ChordMemory.h"
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
TEST_CASE ("GridPage: padLayoutConfig ist das 8x8-Push-Raster (64 Pads)", "[grid][ui]")
{
    // User 10.07.2026: 64 Pads — Config-Defaults von PadGridLayout bleiben
    // 8x4, nur die Grid-Page setzt rows explizit; lowestNote 48 bleibt,
    // die Reihen wachsen nach OBEN dazu (+5 HT/Reihe).
    const auto config = conduit::GridPage::padLayoutConfig();
    REQUIRE (config.cols == 8);
    REQUIRE (config.rows == 8);
    REQUIRE (config.lowestNote == 48);

    const grid::PadGridLayout layout (config);
    REQUIRE (layout.noteForPad (7 * 8) == 48);       // unten links unverändert C3
    REQUIRE (layout.noteForPad (0) == 48 + 7 * 5);   // oben links = 83
}

TEST_CASE ("GridKeyboardComponent: Tap in ein Pad startet die erwartete Note", "[grid][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);
    conduit::GridKeyboardComponent keyboard (engine, conduit::GridPage::padLayoutConfig());
    keyboard.setSize (320, 320);   // 8x8 Raster -> 40x40 px pro Pad (Grid-Page-Config)

    // Pad (col=2, row=3 von oben -- rowFromBottom 4): Mitte bei (100, 140)
    keyboard.mouseDown (makeEvent (keyboard, { 100.0f, 140.0f }));

    // noteOn + expliziter Startwert für Bend/Pressure (sonst läse das MPE-
    // Instrument den Alt-Zustand des zuletzt auf diesem Kanal genutzten
    // Voice-Slots statt 0/Ist-Position, Fund 06.07.2026)
    REQUIRE (fake.calls.size() == 3);
    REQUIRE (fake.calls[0].kind == grid::FakeVoiceSink::Kind::VoiceStart);
    REQUIRE (fake.calls[0].voiceIndex == 0);
    REQUIRE (fake.calls[0].intValue == 70);   // lowestNote(48) + col(2) + rowFromBottom(4)*5
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

TEST_CASE ("GridKeyboardComponent: setPressureSensitivity(100) viertelt das Pressure-Delta (Block A2)", "[grid][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::FakeVoiceSink fakeDefault;
    grid::GridVoiceEngine engineDefault (fakeDefault);
    conduit::GridKeyboardComponent keyboardDefault (engineDefault, conduit::GridPage::padLayoutConfig());
    keyboardDefault.setSize (320, 320);

    keyboardDefault.mouseDown (makeEvent (keyboardDefault, { 100.0f, 140.0f }));
    keyboardDefault.mouseDrag (makeEvent (keyboardDefault, { 100.0f, 100.0f })); // nach oben

    grid::FakeVoiceSink fakeSensitive;
    grid::GridVoiceEngine engineSensitive (fakeSensitive);
    conduit::GridKeyboardComponent keyboardSensitive (engineSensitive, conduit::GridPage::padLayoutConfig());
    keyboardSensitive.setSize (320, 320);
    keyboardSensitive.setPressureSensitivity (100.0); // Faktor 4 auf yRangeNorm -> 1/4 Delta

    keyboardSensitive.mouseDown (makeEvent (keyboardSensitive, { 100.0f, 140.0f }));
    keyboardSensitive.mouseDrag (makeEvent (keyboardSensitive, { 100.0f, 100.0f }));

    const auto pressureCall = [] (const grid::FakeVoiceSink& fake)
    {
        for (auto it = fake.calls.rbegin(); it != fake.calls.rend(); ++it)
            if (it->kind == grid::FakeVoiceSink::Kind::Pressure)
                return it->floatValue;
        return 0.0f;
    };

    const auto deltaDefault   = pressureCall (fakeDefault) - 0.5f;
    const auto deltaSensitive = pressureCall (fakeSensitive) - 0.5f;

    REQUIRE (deltaSensitive == Catch::Approx (deltaDefault * 0.25f));
}

TEST_CASE ("GridKeyboardComponent: setPitchBendMultiplier(2) verdoppelt den Bend (Block A3)", "[grid][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::FakeVoiceSink fakeDefault;
    grid::GridVoiceEngine engineDefault (fakeDefault);
    conduit::GridKeyboardComponent keyboardDefault (engineDefault, conduit::GridPage::padLayoutConfig());
    keyboardDefault.setSize (320, 320);

    keyboardDefault.mouseDown (makeEvent (keyboardDefault, { 100.0f, 140.0f }));
    keyboardDefault.mouseDrag (makeEvent (keyboardDefault, { 140.0f, 140.0f }));

    grid::FakeVoiceSink fakeDoubled;
    grid::GridVoiceEngine engineDoubled (fakeDoubled);
    conduit::GridKeyboardComponent keyboardDoubled (engineDoubled, conduit::GridPage::padLayoutConfig());
    keyboardDoubled.setSize (320, 320);
    keyboardDoubled.setPitchBendMultiplier (2.0f);

    keyboardDoubled.mouseDown (makeEvent (keyboardDoubled, { 100.0f, 140.0f }));
    keyboardDoubled.mouseDrag (makeEvent (keyboardDoubled, { 140.0f, 140.0f }));

    const auto bendCall = [] (const grid::FakeVoiceSink& fake)
    {
        for (auto it = fake.calls.rbegin(); it != fake.calls.rend(); ++it)
            if (it->kind == grid::FakeVoiceSink::Kind::PitchBend)
                return it->floatValue;
        return 0.0f;
    };

    REQUIRE (bendCall (fakeDoubled) == Catch::Approx (bendCall (fakeDefault) * 2.0f));
}

TEST_CASE ("GridKeyboardComponent: Wisch ueber n Spalten klingt wie Re-Hit derselben Position (Block B1/B3)", "[grid][ui]")
{
    // User-Befund (Kalibrierungs-Bug): C2 + 8 Pads nach rechts gewischt
    // ergab +14 HT, dieselbe Position neu angeschlagen +7. Isomorph muessen
    // beide Wege denselben klingenden Pitch ergeben: note + bend identisch.
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    const auto soundingPitch = [] (const grid::FakeVoiceSink& fake)
    {
        int note = -1;
        float bend = 0.0f;
        for (const auto& call : fake.calls)
        {
            if (call.kind == grid::FakeVoiceSink::Kind::VoiceStart) note = call.intValue;
            if (call.kind == grid::FakeVoiceSink::Kind::PitchBend)  bend = call.floatValue;
        }
        return (float) note + bend;
    };

    // Weg 1: Anschlag in Pad (col 0, unterste Reihe), 7 Pad-Breiten nach
    // rechts wischen (col 7 = letzte Spalte des 8er-Rasters) --
    // Aufsetzpunkt LEICHT neben dem Pad-Zentrum (12 px), damit der Test
    // den absoluten pad-Anker wirklich prueft.
    grid::FakeVoiceSink fakeDrag;
    grid::GridVoiceEngine engineDrag (fakeDrag);
    conduit::GridKeyboardComponent keyboardDrag (engineDrag, conduit::GridPage::padLayoutConfig());
    keyboardDrag.setSize (320, 320);   // 8x8 -> 40 px/Pad

    keyboardDrag.mouseDown (makeEvent (keyboardDrag, { 32.0f, 300.0f }));
    keyboardDrag.mouseDrag (makeEvent (keyboardDrag, { 32.0f + 7.0f * 40.0f, 300.0f }));

    // Weg 2: dieselbe End-Position frisch anschlagen.
    grid::FakeVoiceSink fakeHit;
    grid::GridVoiceEngine engineHit (fakeHit);
    conduit::GridKeyboardComponent keyboardHit (engineHit, conduit::GridPage::padLayoutConfig());
    keyboardHit.setSize (320, 320);

    keyboardHit.mouseDown (makeEvent (keyboardHit, { 32.0f + 7.0f * 40.0f, 300.0f }));

    REQUIRE (soundingPitch (fakeDrag) == Catch::Approx (soundingPitch (fakeHit)).margin (1.0e-4));

    // Und die Groessenordnung stimmt: 7 Spalten = +7 HT (B3-Kalibrierung).
    REQUIRE (soundingPitch (fakeDrag) - 48.0f == Catch::Approx (7.0f).margin (0.3));
}

TEST_CASE ("GridKeyboardComponent: finger-Modus bendet relativ zum Aufsetzpunkt (Block B1)", "[grid][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);
    conduit::GridKeyboardComponent keyboard (engine, conduit::GridPage::padLayoutConfig());
    keyboard.setSize (320, 320);
    keyboard.setInTuneLocation (grid::InTuneLocation::finger);

    // Aufsetzpunkt neben dem Pad-Zentrum: im finger-Modus trotzdem 0 Bend.
    keyboard.mouseDown (makeEvent (keyboard, { 88.0f, 140.0f }));

    REQUIRE (fake.calls.size() == 3);
    REQUIRE (fake.calls[1].kind == grid::FakeVoiceSink::Kind::PitchBend);
    REQUIRE (juce::exactlyEqual (fake.calls[1].floatValue, 0.0f));
}

TEST_CASE ("GridKeyboardComponent: setSlideSensitivity bleibt konsistent mit dem Akkord-Latch (Block A2)", "[grid][ui]")
{
    // GridKeyboardComponent::latchConstellation baut die Slide-Formel ueber
    // ring.maxRadiusPx()/restRadiusPx() nach (GridKeyboardComponent.cpp) --
    // beide Getter lesen die per setSlideSensitivity gesetzte Config live,
    // bleiben also automatisch konsistent (kein zweiter Wertespeicher).
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);
    conduit::GridKeyboardComponent keyboard (engine, conduit::GridPage::padLayoutConfig());
    keyboard.setSize (320, 320);   // 8x8 Raster -> 40x40 px pro Pad
    keyboard.setSlideSensitivity (0.0); // Faktor 0.25 -> Spanne 40..85 (statt 40..220)

    // Sonne (100,300) -> Note 50; Mond-Offset 85 px (die neue maxRadiusPx
    // bei Sensitivity 0) -> Slide muss exakt 1.0 sein (nicht 0.5, wie es
    // die Default-Spanne 40..220 an dieser Stelle ergeben wuerde).
    keyboard.latchConstellation ({ { 100.0f / 320.0f, 300.0f / 320.0f,
                                     85.0f / 320.0f, 0.0f, true } });

    // Clang -Wshadow-uncaptured-local (CI): Parameter darf nicht wie die
    // lokale Variable "fake" heissen.
    const auto slideCall = [] (const grid::FakeVoiceSink& recorder)
    {
        for (auto it = recorder.calls.rbegin(); it != recorder.calls.rend(); ++it)
            if (it->kind == grid::FakeVoiceSink::Kind::Slide)
                return it->floatValue;
        return -1.0f;
    };

    REQUIRE (slideCall (fake) == Catch::Approx (1.0f));
}

TEST_CASE ("GridKeyboardComponent: unterste Reihe bleibt bei 8 Reihen identisch", "[grid][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);
    conduit::GridKeyboardComponent keyboard (engine, conduit::GridPage::padLayoutConfig());
    keyboard.setSize (320, 320);

    // Pad (col=2, row=7 -- unterste Reihe): Mitte bei (100, 300) -> Note 50
    // wie im früheren 8x4-Raster (lowestNote bleibt unten links).
    keyboard.mouseDown (makeEvent (keyboard, { 100.0f, 300.0f }));

    REQUIRE (fake.calls.size() == 3);
    REQUIRE (fake.calls[0].kind == grid::FakeVoiceSink::Kind::VoiceStart);
    REQUIRE (fake.calls[0].intValue == 50);   // lowestNote(48) + col(2) + rowFromBottom(0)*5

    keyboard.mouseUp (makeEvent (keyboard, { 100.0f, 300.0f }));
    REQUIRE (fake.calls.size() == 4);
    REQUIRE (fake.calls[3].kind == grid::FakeVoiceSink::Kind::VoiceStop);
}

TEST_CASE ("GridKeyboardComponent: Tap außerhalb des Rasters bleibt wirkungslos", "[grid][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);
    conduit::GridKeyboardComponent keyboard (engine, conduit::GridPage::padLayoutConfig());
    keyboard.setSize (320, 320);

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

//==============================================================================
// Akkord-Speicher (Grid-Page v2, Feature 6): latched Konstellation

TEST_CASE ("GridKeyboardComponent: latchConstellation spielt die gespeicherten Noten", "[grid][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);
    conduit::GridKeyboardComponent keyboard (engine, conduit::GridPage::padLayoutConfig());
    keyboard.setSize (320, 320);   // 8x8 Raster -> 40x40 px pro Pad

    // Sonne mit Mond in Pad-Mitte (col=2, row=7 -- unterste Reihe):
    // (100, 300) -> Note 50. Mond-Offset 130 px (ueber die BREITE
    // normalisiert, 130/320) ->
    // Slide = (130 - minRadius 40) / (maxRadius 220 - 40) = 0.5.
    keyboard.latchConstellation ({ { 100.0f / 320.0f, 300.0f / 320.0f,
                                     130.0f / 320.0f, 0.0f, true } });

    REQUIRE (fake.calls.size() == 4);
    REQUIRE (fake.calls[0].kind == grid::FakeVoiceSink::Kind::VoiceStart);
    REQUIRE (fake.calls[0].intValue == 50);   // lowestNote(48) + col(2)
    REQUIRE (fake.calls[0].intValue2 == 100);
    REQUIRE (fake.calls[1].kind == grid::FakeVoiceSink::Kind::PitchBend);
    REQUIRE (juce::exactlyEqual (fake.calls[1].floatValue, 0.0f));
    REQUIRE (fake.calls[2].kind == grid::FakeVoiceSink::Kind::Pressure);
    REQUIRE (juce::exactlyEqual (fake.calls[2].floatValue, 0.5f));   // neutral am Aufsetzpunkt
    REQUIRE (fake.calls[3].kind == grid::FakeVoiceSink::Kind::Slide);
    REQUIRE (fake.calls[3].floatValue == Catch::Approx (0.5));

    // clearLatched beendet den Akkord per noteOff.
    keyboard.clearLatched();
    REQUIRE (fake.calls.size() == 5);
    REQUIRE (fake.calls[4].kind == grid::FakeVoiceSink::Kind::VoiceStop);
    REQUIRE (fake.calls[4].voiceIndex == 0);
}

TEST_CASE ("GridKeyboardComponent: moveLatchedBy verschiebt starr und aktualisiert Bend/Pressure", "[grid][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);
    conduit::GridKeyboardComponent keyboard (engine, conduit::GridPage::padLayoutConfig());
    keyboard.setSize (320, 320);

    keyboard.latchConstellation ({ { 100.0f / 320.0f, 300.0f / 320.0f, 0.0f, 0.0f, false } });
    REQUIRE (fake.calls.size() == 3);   // Start + Bend 0 + Pressure 0.5 (kein Orbit -> kein Slide)

    // 1 Pad nach rechts (40 px = 1 HT, Block-B3-Kalibrierung: 1 Pad-Breite
    // = 1 Halbton) und 1 Pad hoch (40 px = +0.125 norm bei Hoehe 320 und
    // yRangeNorm 0.5 -> Pressure 0.5 + 0.25 = 0.75) -- X = Pitch,
    // Y = Ausdruck, exakt wie ein Finger-Drag (Latch bleibt LINEAR, ohne
    // In-Tune-Zone).
    keyboard.moveLatchedBy (40.0f, -40.0f);

    REQUIRE (fake.calls.size() == 5);
    REQUIRE (fake.calls[3].kind == grid::FakeVoiceSink::Kind::PitchBend);
    REQUIRE (fake.calls[3].floatValue == Catch::Approx (1.0).margin (1.0e-4));
    REQUIRE (fake.calls[4].kind == grid::FakeVoiceSink::Kind::Pressure);
    REQUIRE (fake.calls[4].floatValue == Catch::Approx (0.75));
}

TEST_CASE ("GridKeyboardComponent: latched Sonne ausserhalb des Rasters spielt keine Note", "[grid][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);
    conduit::GridKeyboardComponent keyboard (engine, conduit::GridPage::padLayoutConfig());
    keyboard.setSize (320, 320);

    // x = 1.5 liegt rechts ausserhalb -> kein noteOn, aber visuell gelatched
    // (constellationNormalized fuehrt die Sonne weiter).
    keyboard.latchConstellation ({ { 1.5f, 0.5f, 0.0f, 0.0f, false } });

    REQUIRE (fake.calls.empty());
    REQUIRE (keyboard.constellationNormalized().size() == 1);

    // Verschieben und Loeschen bleiben ohne Engine-Aufrufe (note = -1).
    keyboard.moveLatchedBy (-10.0f, 0.0f);
    keyboard.clearLatched();
    REQUIRE (fake.calls.empty());
    REQUIRE (keyboard.constellationNormalized().empty());
}

TEST_CASE ("GridKeyboardComponent: constellationNormalized liefert live Finger in ChordMemory-Konvention", "[grid][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);
    conduit::GridKeyboardComponent keyboard (engine, conduit::GridPage::padLayoutConfig());
    keyboard.setSize (320, 320);

    keyboard.mouseDown (makeEvent (keyboard, { 100.0f, 140.0f }));

    const auto suns = keyboard.constellationNormalized();
    REQUIRE (suns.size() == 1);
    REQUIRE (suns[0].x == Catch::Approx (100.0 / 320.0));
    REQUIRE (suns[0].y == Catch::Approx (140.0 / 320.0));
    REQUIRE_FALSE (suns[0].hasOrbit);

    keyboard.mouseUp (makeEvent (keyboard, { 100.0f, 140.0f }));
    REQUIRE (keyboard.constellationNormalized().empty());
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

//==============================================================================
// Oktav-Shift (Block D2)

TEST_CASE ("GridKeyboardComponent: octaveUp/octaveDown verschieben um 12 Halbtoene", "[grid][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);
    conduit::GridKeyboardComponent keyboard (engine, conduit::GridPage::padLayoutConfig());
    keyboard.setSize (320, 320);

    REQUIRE (keyboard.octaveShift() == 0);

    keyboard.octaveUp();
    REQUIRE (keyboard.octaveShift() == 1);

    keyboard.mouseDown (makeEvent (keyboard, { 100.0f, 140.0f }));   // Note 70 bei Shift 0
    REQUIRE (fake.calls[0].intValue == 82);   // 70 + 12
    keyboard.mouseUp (makeEvent (keyboard, { 100.0f, 140.0f }));

    keyboard.octaveDown();
    keyboard.octaveDown();
    REQUIRE (keyboard.octaveShift() == -1);

    fake.calls.clear();
    keyboard.mouseDown (makeEvent (keyboard, { 100.0f, 140.0f }));
    REQUIRE (fake.calls[0].intValue == 58);   // 70 - 12
}

TEST_CASE ("GridKeyboardComponent: octaveShift klemmt auf +-kMaxOctaveShift", "[grid][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);
    conduit::GridKeyboardComponent keyboard (engine, conduit::GridPage::padLayoutConfig());

    for (int i = 0; i < 10; ++i)
        keyboard.octaveUp();
    REQUIRE (keyboard.octaveShift() == conduit::GridKeyboardComponent::kMaxOctaveShift);

    for (int i = 0; i < 20; ++i)
        keyboard.octaveDown();
    REQUIRE (keyboard.octaveShift() == -conduit::GridKeyboardComponent::kMaxOctaveShift);
}

//==============================================================================
// Noten-Echo (Block H4): externe Noten leuchten ohne Sonne/Mond

TEST_CASE ("GridKeyboard: Noten-Echo setzt und loescht Pegel (geclampt)", "[grid][echo]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);
    conduit::GridKeyboardComponent keyboard (engine);

    REQUIRE (keyboard.echoLevel (60) == 0.0f);

    keyboard.echoNoteOn (60, 0.8f);
    REQUIRE (keyboard.echoLevel (60) == Catch::Approx (0.8f));

    keyboard.echoNoteOn (61, 5.0f);   // geclampt auf 1
    REQUIRE (keyboard.echoLevel (61) == Catch::Approx (1.0f));

    keyboard.echoNoteOff (60);
    REQUIRE (keyboard.echoLevel (60) == 0.0f);
    REQUIRE (keyboard.echoLevel (61) == Catch::Approx (1.0f));

    keyboard.clearEchoNotes();
    REQUIRE (keyboard.echoLevel (61) == 0.0f);

    // Ausser-Bereichs-Noten sind wirkungslos (kein Crash)
    keyboard.echoNoteOn (-1, 1.0f);
    keyboard.echoNoteOn (128, 1.0f);
    REQUIRE (keyboard.echoLevel (-1) == 0.0f);
    REQUIRE (keyboard.echoLevel (128) == 0.0f);
}
