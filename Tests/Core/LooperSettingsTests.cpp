#include <memory>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/LooperSettings.h"

using Catch::Approx;
using conduit::LaunchQuant;
using conduit::LooperSettings;

namespace
{

struct TempSettings
{
    TempSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitLooperSettingsTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();
    }

    ~TempSettings() { folder.deleteRecursively(); }

    [[nodiscard]] juce::PropertiesFile::Options options() const
    {
        juce::PropertiesFile::Options result;
        result.applicationName = "LooperSettingsTests";
        result.filenameSuffix  = ".settings";
        result.folderName      = folder.getFullPathName();
        return result;
    }

    juce::File folder;
};

} // namespace

//==============================================================================
TEST_CASE ("LooperSettings: Defaults ohne gespeicherten Zustand", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;
    LooperSettings settings { temp.options() };

    REQUIRE_FALSE (settings.hasStoredState());
    REQUIRE (settings.getLaunchQuant() == LaunchQuant::bar1);
    REQUIRE (settings.getTapMode() == LooperSettings::TapMode::retrigger);
    REQUIRE (settings.getHalveMode() == conduit::looper::HalveMode::firstHalf);
    REQUIRE (settings.getReverseMode() == LooperSettings::ReverseMode::immediate);
    REQUIRE (settings.getVariRaster() == LooperSettings::VariRaster::semitones);
    REQUIRE (settings.getVariScope() == LooperSettings::VariScope::perTrack);
    REQUIRE (settings.getSoloScope() == LooperSettings::SoloScope::perLooper);
    REQUIRE (settings.getVisibleSlots() == 8);
    REQUIRE_FALSE (settings.isDeleteLatchEnabled());
    REQUIRE (settings.isAutoAdvanceEnabled());
    REQUIRE (settings.getNumLoopers() == 1);

    // Werks-Default OHNE Quelle (19.07.2026: "master" ist seit ADR 010
    // nicht mehr wählbar und blockierte gearmt die Puffersatz-Erweiterung)
    REQUIRE (settings.getSourceKey (0).isEmpty());
    REQUIRE (settings.getSourceKey (1).isEmpty());
    REQUIRE (settings.getTrackGain (0, 0) == Approx (1.0f));
    REQUIRE (settings.getTrackPan (0, 0) == Approx (0.0f));
    REQUIRE_FALSE (settings.isTrackMuted (0, 0));
    REQUIRE_FALSE (settings.isTrackSolo (0, 0));
    REQUIRE_FALSE (settings.isTrackVariQuantized (0, 0));   // frei = Default
}

TEST_CASE ("LooperSettings: Roundtrip über Neuinstanz", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;

    {
        LooperSettings settings { temp.options() };
        settings.setLaunchQuant (LaunchQuant::off);
        settings.setTapMode (LooperSettings::TapMode::toggleStop);
        settings.setHalveMode (conduit::looper::HalveMode::currentHalf);
        settings.setReverseMode (LooperSettings::ReverseMode::boundary);
        settings.setVariRaster (LooperSettings::VariRaster::sessionScale);
        settings.setVariScope (LooperSettings::VariScope::perLooper);
        settings.setSoloScope (LooperSettings::SoloScope::globalScope);
        settings.setVisibleSlots (11);
        settings.setDeleteLatchEnabled (true);
        settings.setAutoAdvanceEnabled (false);
        settings.setNumLoopers (3);

        settings.setSourceKey (0, "hw:0");
        settings.setSourceKey (2, "tap:delay_1");
        settings.setSpectrumView (2, true);
        settings.setNumTracks (2, 3);
        settings.setTrackGain (2, 1, 0.5f);
        settings.setTrackPan (2, 1, -0.25f);
        settings.setTrackMuted (2, 1, true);
        settings.setTrackSolo (0, 0, true);
        settings.setTrackVariQuantized (2, 1, true);
        settings.flush();
    }

    LooperSettings reloaded { temp.options() };
    REQUIRE (reloaded.hasStoredState());
    REQUIRE (reloaded.getLaunchQuant() == LaunchQuant::off);
    REQUIRE (reloaded.getTapMode() == LooperSettings::TapMode::toggleStop);
    REQUIRE (reloaded.getHalveMode() == conduit::looper::HalveMode::currentHalf);
    REQUIRE (reloaded.getReverseMode() == LooperSettings::ReverseMode::boundary);
    REQUIRE (reloaded.getVariRaster() == LooperSettings::VariRaster::sessionScale);
    REQUIRE (reloaded.getVariScope() == LooperSettings::VariScope::perLooper);
    REQUIRE (reloaded.getSoloScope() == LooperSettings::SoloScope::globalScope);
    REQUIRE (reloaded.getVisibleSlots() == 11);
    REQUIRE (reloaded.isDeleteLatchEnabled());
    REQUIRE_FALSE (reloaded.isAutoAdvanceEnabled());
    REQUIRE (reloaded.getNumLoopers() == 3);

    REQUIRE (reloaded.getSourceKey (0) == "hw:0");
    REQUIRE (reloaded.getSourceKey (2) == "tap:delay_1");
    REQUIRE (reloaded.isSpectrumView (2));
    REQUIRE (reloaded.getNumTracks (2) == 3);
    REQUIRE (reloaded.getTrackGain (2, 1) == Approx (0.5f));
    REQUIRE (reloaded.getTrackPan (2, 1) == Approx (-0.25f));
    REQUIRE (reloaded.isTrackMuted (2, 1));
    REQUIRE (reloaded.isTrackSolo (0, 0));
    REQUIRE (reloaded.isTrackVariQuantized (2, 1));
}

TEST_CASE ("LooperSettings: Clamps und ungültige Indizes", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;
    LooperSettings settings { temp.options() };

    settings.setVisibleSlots (99);
    REQUIRE (settings.getVisibleSlots() == LooperSettings::maxVisibleSlots);
    settings.setVisibleSlots (1);
    REQUIRE (settings.getVisibleSlots() == LooperSettings::minVisibleSlots);

    settings.setNumLoopers (0);
    REQUIRE (settings.getNumLoopers() == 1);
    settings.setNumLoopers (9);
    REQUIRE (settings.getNumLoopers() == LooperSettings::maxLoopers);

    settings.setTrackGain (0, 0, 5.0f);
    REQUIRE (settings.getTrackGain (0, 0) == Approx (2.0f));
    settings.setTrackPan (0, 0, -3.0f);
    REQUIRE (settings.getTrackPan (0, 0) == Approx (-1.0f));

    // Out-of-range: No-ops, Getter liefern Defaults
    settings.setSourceKey (7, "hw:0");
    REQUIRE (settings.getSourceKey (7).isEmpty());
    settings.setTrackGain (0, 9, 0.1f);
    REQUIRE (settings.getTrackGain (0, 9) == Approx (1.0f));
}

TEST_CASE ("LooperSettings: Einmal-Migration der Legacy-Schlüssel", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;

    SECTION ("frisch: Migration übernimmt Quelle + Spektrum für Looper 0")
    {
        LooperSettings settings { temp.options() };
        settings.migrateFromLegacy ("hw:1", true);

        REQUIRE (settings.getSourceKey (0) == "hw:1");
        REQUIRE (settings.isSpectrumView (0));
    }

    SECTION ("mit gespeichertem Zustand: Migration ist ein No-op")
    {
        {
            LooperSettings settings { temp.options() };
            settings.setSourceKey (0, "tap:synth");
            settings.flush();
        }

        LooperSettings reloaded { temp.options() };
        reloaded.migrateFromLegacy ("hw:1", true);

        REQUIRE (reloaded.getSourceKey (0) == "tap:synth");
        REQUIRE_FALSE (reloaded.isSpectrumView (0));
    }
}
