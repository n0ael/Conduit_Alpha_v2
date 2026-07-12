#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/GridPanelSettings.h"
#include "UI/ConduitColorPicker.h"
#include "UI/PushLookAndFeel.h"

using Catch::Approx;
using conduit::ConduitColorPicker;

namespace
{

/** Persistenz in ein Temp-Verzeichnis statt in die echte Settings-Datei
    (Muster TempMeterSettings). */
struct TempGridPanelSettings
{
    TempGridPanelSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitGridPanelSettingsTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();
    }

    ~TempGridPanelSettings() { folder.deleteRecursively(); }

    [[nodiscard]] juce::PropertiesFile::Options options() const
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "ConduitGridPanelSettingsTests";
        o.filenameSuffix  = ".settings";
        o.folderName      = folder.getFullPathName();  // absoluter Pfad
        return o;
    }

    juce::File folder;
};

} // namespace

//==============================================================================
TEST_CASE ("ConduitColorPicker: fromHsv trifft bekannte RGB-Werte", "[colorpicker]")
{
    // Primärfarben (h01-Konvention: 0 = Rot, 1/3 = Grün, 2/3 = Blau)
    REQUIRE (ConduitColorPicker::fromHsv (0.0f, 1.0f, 1.0f) == juce::Colour (0xffff0000));
    REQUIRE (ConduitColorPicker::fromHsv (1.0f / 3.0f, 1.0f, 1.0f) == juce::Colour (0xff00ff00));
    REQUIRE (ConduitColorPicker::fromHsv (2.0f / 3.0f, 1.0f, 1.0f) == juce::Colour (0xff0000ff));

    // Sekundärfarben
    REQUIRE (ConduitColorPicker::fromHsv (1.0f / 6.0f, 1.0f, 1.0f) == juce::Colour (0xffffff00));
    REQUIRE (ConduitColorPicker::fromHsv (0.5f, 1.0f, 1.0f) == juce::Colour (0xff00ffff));
    REQUIRE (ConduitColorPicker::fromHsv (5.0f / 6.0f, 1.0f, 1.0f) == juce::Colour (0xffff00ff));

    // Grau/Schwarz/Weiß: s bzw. v dominiert, Hue egal
    REQUIRE (ConduitColorPicker::fromHsv (0.0f, 0.0f, 1.0f) == juce::Colour (0xffffffff));
    REQUIRE (ConduitColorPicker::fromHsv (0.42f, 1.0f, 0.0f) == juce::Colour (0xff000000));
    REQUIRE (ConduitColorPicker::fromHsv (0.42f, 0.0f, 240.0f / 255.0f) == juce::Colour (0xfff0f0f0));
}

TEST_CASE ("ConduitColorPicker: toHsv liefert bekannte HSV-Werte", "[colorpicker]")
{
    float h = -1.0f, s = -1.0f, v = -1.0f;

    ConduitColorPicker::toHsv (juce::Colour (0xffff0000), h, s, v);
    REQUIRE (h == Approx (0.0f).margin (1.0e-6f));
    REQUIRE (s == Approx (1.0f));
    REQUIRE (v == Approx (1.0f));

    // ledOrange #ffa726: h = ((167-38)/217)/6, s = 217/255, v = 1
    ConduitColorPicker::toHsv (juce::Colour (0xffffa726), h, s, v);
    REQUIRE (h == Approx ((129.0f / 217.0f) / 6.0f).margin (1.0e-4f));
    REQUIRE (s == Approx (217.0f / 255.0f).margin (1.0e-4f));
    REQUIRE (v == Approx (1.0f));

    // Graustufe: s = 0, Hue-Konvention 0
    ConduitColorPicker::toHsv (juce::Colour (0xfff0f0f0), h, s, v);
    REQUIRE (h == Approx (0.0f).margin (1.0e-6f));
    REQUIRE (s == Approx (0.0f).margin (1.0e-6f));
    REQUIRE (v == Approx (240.0f / 255.0f).margin (1.0e-4f));
}

TEST_CASE ("ConduitColorPicker: HSV-Roundtrip ist 8-bit-exakt", "[colorpicker]")
{
    const juce::uint32 knownColours[] = {
        0xffff0000,   // rot
        0xfff0f0f0,   // ledWhite
        0xffffa726,   // ledOrange
        0xff00bfd8,   // ledCyan
        0xff3ddc84,   // ledGreen
        0xffff453a,   // ledRed
        0xff808080,   // Graustufe (s = 0)
        0xff123456,   // beliebiger Mischwert
        0xffd92662,   // Preset-Rasterwert
        0xff000000,   // schwarz
    };

    for (const auto argb : knownColours)
    {
        const juce::Colour original (argb);

        float h = 0.0f, s = 0.0f, v = 0.0f;
        ConduitColorPicker::toHsv (original, h, s, v);

        REQUIRE (ConduitColorPicker::fromHsv (h, s, v) == original);
    }
}

TEST_CASE ("ConduitColorPicker: Hue wrappt ausserhalb [0,1)", "[colorpicker]")
{
    const auto red = ConduitColorPicker::fromHsv (0.0f, 1.0f, 1.0f);
    REQUIRE (ConduitColorPicker::fromHsv (1.0f, 1.0f, 1.0f) == red);
    REQUIRE (ConduitColorPicker::fromHsv (2.0f, 1.0f, 1.0f) == red);

    REQUIRE (ConduitColorPicker::fromHsv (-0.25f, 1.0f, 1.0f)
             == ConduitColorPicker::fromHsv (0.75f, 1.0f, 1.0f));
    REQUIRE (ConduitColorPicker::fromHsv (1.25f, 1.0f, 1.0f)
             == ConduitColorPicker::fromHsv (0.25f, 1.0f, 1.0f));
}

//==============================================================================
TEST_CASE ("GridPanelSettings: Achsen-Farben starten mit den Token-Defaults", "[gridpanelsettings]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempGridPanelSettings temp;
    conduit::GridPanelSettings settings (temp.options());

    using Axis = conduit::grid::GridVoiceEngine::Axis;
    REQUIRE (settings.getAxisColour (Axis::Pressure)  == conduit::push::colours::ledOrange);
    REQUIRE (settings.getAxisColour (Axis::Slide)     == conduit::push::colours::ledCyan);
    REQUIRE (settings.getAxisColour (Axis::PitchBend) == conduit::push::colours::ledGreen);
}

TEST_CASE ("GridPanelSettings: Achsen-Farbe uebersteht den Roundtrip", "[gridpanelsettings]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempGridPanelSettings temp;

    using Axis = conduit::grid::GridVoiceEngine::Axis;
    const juce::Colour custom (0xff123456);

    {
        conduit::GridPanelSettings settings (temp.options());
        settings.setAxisColour (Axis::Slide, custom);
        REQUIRE (settings.getAxisColour (Axis::Slide) == custom);
    }

    conduit::GridPanelSettings reloaded (temp.options());
    REQUIRE (reloaded.getAxisColour (Axis::Slide) == custom);

    // Die anderen Achsen bleiben auf ihren Defaults
    REQUIRE (reloaded.getAxisColour (Axis::Pressure)  == conduit::push::colours::ledOrange);
    REQUIRE (reloaded.getAxisColour (Axis::PitchBend) == conduit::push::colours::ledGreen);
}

TEST_CASE ("GridPanelSettings: gridLayoutMode default fullPads + Roundtrip", "[gridpanelsettings]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempGridPanelSettings temp;

    using Mode = conduit::GridPanelSettings::GridLayoutMode;

    {
        conduit::GridPanelSettings settings (temp.options());
        REQUIRE (settings.getGridLayoutMode() == Mode::fullPads);   // Default 0

        settings.setGridLayoutMode (Mode::xyFaders);
        REQUIRE (settings.getGridLayoutMode() == Mode::xyFaders);
    }

    {
        conduit::GridPanelSettings reloaded (temp.options());
        REQUIRE (reloaded.getGridLayoutMode() == Mode::xyFaders);
        reloaded.setGridLayoutMode (Mode::fullPads);
    }

    conduit::GridPanelSettings again (temp.options());
    REQUIRE (again.getGridLayoutMode() == Mode::fullPads);
}

//==============================================================================
// Block D1: systemControlRows / ribbonWidthPx / modwheelEnabled

TEST_CASE ("GridPanelSettings: systemControlRows Default + Clamp + Roundtrip", "[gridpanelsettings]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempGridPanelSettings temp;

    {
        conduit::GridPanelSettings settings (temp.options());
        REQUIRE (settings.getSystemControlRows()
                 == conduit::GridPanelSettings::defaultSystemControlRows);

        settings.setSystemControlRows (999);   // ueber max geklemmt
        REQUIRE (settings.getSystemControlRows()
                 == conduit::GridPanelSettings::maxSystemControlRows);

        settings.setSystemControlRows (3);
    }

    conduit::GridPanelSettings reloaded (temp.options());
    REQUIRE (reloaded.getSystemControlRows() == 3);
}

TEST_CASE ("GridPanelSettings: ribbonWidthPx Default + Clamp + Roundtrip", "[gridpanelsettings]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempGridPanelSettings temp;

    {
        conduit::GridPanelSettings settings (temp.options());
        REQUIRE (settings.getRibbonWidthPx() == conduit::GridPanelSettings::defaultRibbonWidthPx);

        settings.setRibbonWidthPx (5);   // unter min geklemmt
        REQUIRE (settings.getRibbonWidthPx() == conduit::GridPanelSettings::minRibbonWidthPx);

        settings.setRibbonWidthPx (100);
    }

    conduit::GridPanelSettings reloaded (temp.options());
    REQUIRE (reloaded.getRibbonWidthPx() == 100);
}

TEST_CASE ("GridPanelSettings: modwheelEnabled Default false + Roundtrip", "[gridpanelsettings]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempGridPanelSettings temp;

    {
        conduit::GridPanelSettings settings (temp.options());
        REQUIRE_FALSE (settings.isModwheelEnabled());

        settings.setModwheelEnabled (true);
        REQUIRE (settings.isModwheelEnabled());
    }

    conduit::GridPanelSettings reloaded (temp.options());
    REQUIRE (reloaded.isModwheelEnabled());
}

//==============================================================================
// Block H v2 rev5: masterMidiInputName / gridMidiInputName

TEST_CASE ("GridPanelSettings: Track-Fokus-Routing-Werte Default + Roundtrip", "[gridpanelsettings]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempGridPanelSettings temp;

    {
        conduit::GridPanelSettings settings (temp.options());
        REQUIRE (settings.getMasterMidiInputName().isEmpty());
        REQUIRE (settings.getGridMidiInputName().isEmpty());
        REQUIRE (settings.getMasterMidiFavourites().isEmpty());

        settings.setMasterMidiInputName ("FromPush");
        settings.setGridMidiInputName ("Conduit Grid MPE");
        settings.setMasterMidiFavourites ({ "FromPush", "K1 (Port 1)" });

        // Block H3 Runde 3: Track-Tabs-Darstellung
        REQUIRE_FALSE (settings.isTrackTabsBottom());
        REQUIRE (settings.getTrackTabsFontPx() == conduit::GridPanelSettings::defaultTrackTabsFontPx);
        settings.setTrackTabsBottom (true);
        settings.setTrackTabsFontPx (999);   // geklemmt auf max
        settings.setTrackTabMinWidthPx (120);
    }

    conduit::GridPanelSettings reloaded (temp.options());
    REQUIRE (reloaded.getMasterMidiInputName() == "FromPush");
    REQUIRE (reloaded.getGridMidiInputName() == "Conduit Grid MPE");
    REQUIRE (reloaded.getMasterMidiFavourites().size() == 2);
    REQUIRE (reloaded.getMasterMidiFavourites()[1] == "K1 (Port 1)");
    REQUIRE (reloaded.isTrackTabsBottom());
    REQUIRE (reloaded.getTrackTabsFontPx() == conduit::GridPanelSettings::maxTrackTabsFontPx);
    REQUIRE (reloaded.getTrackTabMinWidthPx() == 120);
}

//==============================================================================
// Block J (Physics): Toggles + Feder-/Gravity-Tuning

TEST_CASE ("GridPanelSettings: Physics-Werte Default + Clamp + Roundtrip", "[gridpanelsettings]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempGridPanelSettings temp;

    {
        conduit::GridPanelSettings settings (temp.options());
        REQUIRE_FALSE (settings.isGridGravityEnabled());
        REQUIRE_FALSE (settings.isControlPhysicsEnabled());
        REQUIRE_FALSE (settings.isControlSnapToDefault());
        REQUIRE (settings.getPhysicsForce()
                 == Approx (conduit::GridPanelSettings::defaultPhysicsForce));
        REQUIRE (settings.getPhysicsMass()
                 == Approx (conduit::GridPanelSettings::defaultPhysicsMass));
        REQUIRE (settings.getPhysicsInertia() == conduit::GridPanelSettings::defaultPhysicsInertia);
        REQUIRE (settings.getGravityDelayMs() == conduit::GridPanelSettings::defaultGravityDelayMs);
        REQUIRE (settings.getGravityThreshold()
                 == Approx (conduit::GridPanelSettings::defaultGravityThreshold));
        REQUIRE (settings.getGravityFadeMs() == conduit::GridPanelSettings::defaultGravityFadeMs);

        settings.setGridGravityEnabled (true);
        settings.setControlPhysicsEnabled (true);
        settings.setControlSnapToDefault (true);
        settings.setPhysicsForce (999999.0);   // geklemmt auf max
        settings.setPhysicsMass (2.5);
        settings.setPhysicsInertia (-5);       // geklemmt auf min
        settings.setGravityDelayMs (300);
        settings.setGravityThreshold (1.25);
        settings.setGravityFadeMs (500);
    }

    conduit::GridPanelSettings reloaded (temp.options());
    REQUIRE (reloaded.isGridGravityEnabled());
    REQUIRE (reloaded.isControlPhysicsEnabled());
    REQUIRE (reloaded.isControlSnapToDefault());
    REQUIRE (reloaded.getPhysicsForce() == Approx (conduit::GridPanelSettings::maxPhysicsForce));
    REQUIRE (reloaded.getPhysicsMass() == Approx (2.5));
    REQUIRE (reloaded.getPhysicsInertia() == conduit::GridPanelSettings::minPhysicsInertia);
    REQUIRE (reloaded.getGravityDelayMs() == 300);
    REQUIRE (reloaded.getGravityThreshold() == Approx (1.25));
    REQUIRE (reloaded.getGravityFadeMs() == 500);
}

//==============================================================================
// Block K: Sensitivity/Bend-Range/In-Tune/Expression/Oktav-Shift

TEST_CASE ("GridPanelSettings: Block-K-Skalare Default + Clamp + Roundtrip", "[gridpanelsettings]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempGridPanelSettings temp;

    {
        conduit::GridPanelSettings settings (temp.options());
        REQUIRE (settings.getPressureSensitivity()
                 == Approx (conduit::GridPanelSettings::defaultSensitivity));
        REQUIRE (settings.getSlideSensitivity()
                 == Approx (conduit::GridPanelSettings::defaultSensitivity));
        REQUIRE (settings.getBendRangeIndex() == conduit::GridPanelSettings::defaultBendRangeIndex);
        REQUIRE (settings.isInTuneLocationPad());
        REQUIRE (settings.getInTuneWidthPercent()
                 == Approx (conduit::GridPanelSettings::defaultInTuneWidthPercent));
        REQUIRE (settings.getExpressionModeIndex() == 0);
        REQUIRE (settings.getOctaveShift() == 0);

        settings.setPressureSensitivity (70.0);
        settings.setSlideSensitivity (150.0);   // geklemmt auf 100
        settings.setBendRangeIndex (5);
        settings.setInTuneLocationPad (false);
        settings.setInTuneWidthPercent (35.0);
        settings.setExpressionModeIndex (2);
        settings.setOctaveShift (-9);           // geklemmt auf -3
    }

    conduit::GridPanelSettings reloaded (temp.options());
    REQUIRE (reloaded.getPressureSensitivity() == Approx (70.0));
    REQUIRE (reloaded.getSlideSensitivity() == Approx (100.0));
    REQUIRE (reloaded.getBendRangeIndex() == 5);
    REQUIRE_FALSE (reloaded.isInTuneLocationPad());
    REQUIRE (reloaded.getInTuneWidthPercent() == Approx (35.0));
    REQUIRE (reloaded.getExpressionModeIndex() == 2);
    REQUIRE (reloaded.getOctaveShift() == -conduit::GridPanelSettings::maxOctaveShift);

    // Session-Datei liegt neben der Settings-Datei (Temp-Ordner der Tests).
    REQUIRE (reloaded.sessionFile().getFileName() == "GridSession.xml");
    REQUIRE (reloaded.sessionFile().getParentDirectory().getFullPathName()
                 .contains ("ConduitGridPanelSettingsTests"));
}

TEST_CASE ("GridPanelSettings: MIDI-Geraete-Auswahlen Roundtrip (Block K2)", "[gridpanelsettings]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempGridPanelSettings temp;

    {
        conduit::GridPanelSettings settings (temp.options());
        REQUIRE (settings.getGridMidiOutDeviceName().isEmpty());
        REQUIRE (settings.getControlMidiInDeviceName().isEmpty());
        REQUIRE (settings.getEchoMidiInDeviceName().isEmpty());

        settings.setGridMidiOutDeviceName ("Conduit Grid MPE");
        settings.setControlMidiInDeviceName ("K1 (Port 1)");
        settings.setEchoMidiInDeviceName ("Conduit DAW");
    }

    conduit::GridPanelSettings reloaded (temp.options());
    REQUIRE (reloaded.getGridMidiOutDeviceName() == "Conduit Grid MPE");
    REQUIRE (reloaded.getControlMidiInDeviceName() == "K1 (Port 1)");
    REQUIRE (reloaded.getEchoMidiInDeviceName() == "Conduit DAW");

    // Abwaehlen persistiert den Leereintrag.
    reloaded.setEchoMidiInDeviceName ({});
    conduit::GridPanelSettings again (temp.options());
    REQUIRE (again.getEchoMidiInDeviceName().isEmpty());
}
