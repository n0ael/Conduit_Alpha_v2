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
    REQUIRE (settings.getTrackSends (0, 0) == 0);            // keine Sends ab Werk
    REQUIRE_FALSE (settings.isTrackSendPre (0, 0));          // Abgriff post
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
        settings.setTrackSends (2, 1, 0b1010);
        settings.setTrackSendPre (2, 1, true);
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
    REQUIRE (reloaded.getTrackSends (2, 1) == 0b1010);
    REQUIRE (reloaded.isTrackSendPre (2, 1));
    REQUIRE (reloaded.getTrackSends (0, 0) == 0);   // Alt-Zustand: Defaults
    REQUIRE_FALSE (reloaded.isTrackSendPre (0, 0));
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

    // Send-Maske clampt auf Bits 0..3; ungültige Indizes = No-op
    settings.setTrackSends (0, 0, 0xFF);
    REQUIRE (settings.getTrackSends (0, 0) == 0xF);
    settings.setTrackSends (0, 9, 1);
    REQUIRE (settings.getTrackSends (0, 9) == 0);
}

TEST_CASE ("LooperSettings: Send-Level — Roundtrip + Legacy-Bitmasken-Migration", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;

    SECTION ("Level-Roundtrip; abgeleitete Maske folgt Level > 0")
    {
        {
            LooperSettings settings { temp.options() };
            settings.setTrackSendLevel (1, 2, 0, 0.25f);
            settings.setTrackSendLevel (1, 2, 3, 1.0f);
            settings.setTrackSendLevel (0, 0, 1, 2.0f);   // clampt auf 1.0
            settings.flush();
        }

        LooperSettings reloaded { temp.options() };
        REQUIRE (reloaded.getTrackSendLevel (1, 2, 0) == Approx (0.25f));
        REQUIRE (reloaded.getTrackSendLevel (1, 2, 3) == Approx (1.0f));
        REQUIRE (reloaded.getTrackSendLevel (1, 2, 1) == Approx (0.0f));
        REQUIRE (reloaded.getTrackSendLevel (0, 0, 1) == Approx (1.0f));
        REQUIRE (reloaded.getTrackSends (1, 2) == 0b1001);

        // Masken-Setter plättet feine Level nicht (Bit gesetzt + Level > 0
        // bleibt unangetastet); gelöschtes Bit nullt den Level
        reloaded.setTrackSends (1, 2, 0b1001);
        REQUIRE (reloaded.getTrackSendLevel (1, 2, 0) == Approx (0.25f));
        reloaded.setTrackSends (1, 2, 0b0001);
        REQUIRE (reloaded.getTrackSendLevel (1, 2, 3) == Approx (0.0f));
        REQUIRE (reloaded.getTrackSendLevel (1, 2, 0) == Approx (0.25f));

        // Ungültiger Send-Index = No-op
        reloaded.setTrackSendLevel (0, 0, 7, 1.0f);
        REQUIRE (reloaded.getTrackSendLevel (0, 0, 7) == Approx (0.0f));
    }

    SECTION ("Alt-Datei mit sends-Bitmaske lädt als Level 1.0 pro Bit")
    {
        {
            juce::PropertiesFile legacy { temp.options() };
            juce::XmlElement root ("LooperState");
            auto* looper = root.createNewChildElement ("Looper");
            looper->setAttribute ("source", "hw:0");
            auto* track = looper->createNewChildElement ("Track");
            track->setAttribute ("gain", 1.0);
            track->setAttribute ("sends", 0b0101);
            track->setAttribute ("sendPre", true);
            legacy.setValue ("looperState", &root);
            legacy.save();
        }

        LooperSettings settings { temp.options() };
        REQUIRE (settings.hasStoredState());
        REQUIRE (settings.getTrackSendLevel (0, 0, 0) == Approx (1.0f));
        REQUIRE (settings.getTrackSendLevel (0, 0, 1) == Approx (0.0f));
        REQUIRE (settings.getTrackSendLevel (0, 0, 2) == Approx (1.0f));
        REQUIRE (settings.getTrackSends (0, 0) == 0b0101);
        REQUIRE (settings.isTrackSendPre (0, 0));
    }
}

TEST_CASE ("LooperSettings: Panel-Optionen 07/2026 — Defaults + Roundtrip", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;

    {
        LooperSettings settings { temp.options() };

        REQUIRE (settings.getVariDisplay() == LooperSettings::VariDisplay::semitones);
        REQUIRE (settings.getSendCount() == 4);
        REQUIRE (settings.isShowStopAll());
        REQUIRE_FALSE (settings.isHideMuteSolo());
        REQUIRE_FALSE (settings.isHideMixerXy());
        REQUIRE (settings.getPanelCollapsedMask() == 0);

        settings.setTapMode (LooperSettings::TapMode::legato);
        settings.setReverseMode (LooperSettings::ReverseMode::quantized);
        settings.setVariScope (LooperSettings::VariScope::globalScope);
        settings.setVariDisplay (LooperSettings::VariDisplay::scaleDegrees);
        settings.setSendCount (2);
        settings.setShowStopAll (false);
        settings.setHideMuteSolo (true);
        settings.setHideMixerXy (true);
        settings.setPanelCollapsedMask (0b101);
        settings.setSendCount (99);   // clampt auf 4
        settings.flush();
    }

    LooperSettings reloaded { temp.options() };
    REQUIRE (reloaded.getTapMode() == LooperSettings::TapMode::legato);
    REQUIRE (reloaded.getReverseMode() == LooperSettings::ReverseMode::quantized);
    REQUIRE (reloaded.getVariScope() == LooperSettings::VariScope::globalScope);
    REQUIRE (reloaded.getVariDisplay() == LooperSettings::VariDisplay::scaleDegrees);
    REQUIRE (reloaded.getSendCount() == 4);
    REQUIRE_FALSE (reloaded.isShowStopAll());
    REQUIRE (reloaded.isHideMuteSolo());
    REQUIRE (reloaded.isHideMixerXy());
    REQUIRE (reloaded.getPanelCollapsedMask() == 0b101);
}

TEST_CASE ("LooperSettings: Distanz — Defaults, Clamps, Roundtrip, Y-Link", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;

    {
        LooperSettings settings { temp.options() };

        // Defaults: Vol Dump AN (User 20.07.2026), Y-Link aus
        const auto defaults = settings.getDistance();
        REQUIRE (defaults.volDumpOn);
        REQUIRE (defaults.hiDumpDb == Approx (9.0f));
        REQUIRE (defaults.ySens == Approx (1.0f));
        REQUIRE (settings.getYLinkSend() == -1);
        REQUIRE (settings.getTrackDistance (0, 0) == Approx (0.0f));

        auto state = defaults;
        state.hiDumpDb = 99.0f;        // clampt auf 18
        state.hiCutHz = 6100.0f;
        state.baseFreqHz = 663.0f;
        state.width01 = 0.4f;
        state.volDumpOn = false;
        state.smoothMs = 125.0f;
        state.ySens = 0.7f;
        settings.setDistance (state);
        settings.setYLinkSend (2);
        settings.setTrackDistance (1, 3, 0.65f);
        settings.setTrackDistance (0, 0, 7.0f);   // clampt auf 1
        settings.flush();
    }

    LooperSettings reloaded { temp.options() };
    const auto dist = reloaded.getDistance();
    REQUIRE (dist.hiDumpDb == Approx (18.0f));
    REQUIRE (dist.hiCutHz == Approx (6100.0f));
    REQUIRE (dist.baseFreqHz == Approx (663.0f));
    REQUIRE (dist.width01 == Approx (0.4f));
    REQUIRE_FALSE (dist.volDumpOn);
    REQUIRE (dist.smoothMs == Approx (125.0f));
    REQUIRE (dist.ySens == Approx (0.7f));
    REQUIRE (reloaded.getYLinkSend() == 2);
    REQUIRE (reloaded.getTrackDistance (1, 3) == Approx (0.65f));
    REQUIRE (reloaded.getTrackDistance (0, 0) == Approx (1.0f));
    REQUIRE (reloaded.getTrackDistance (3, 3) == Approx (0.0f));
}

TEST_CASE ("LooperSettings: Send-Farben — Werks-Palette, Auswahl, Roundtrip", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;

    {
        LooperSettings settings { temp.options() };

        // Ab Werk die Handoff-Palette (● orange · ■ blau · ▲ grün · ⬡ türkis)
        REQUIRE (settings.getSendColour (0) == juce::Colour (0xffff8d28));
        REQUIRE (settings.getSendColour (1) == juce::Colour (0xff6155f5));
        REQUIRE (settings.getSendColour (2) == juce::Colour (0xff34c759));
        REQUIRE (settings.getSendColour (3) == juce::Colour (0xff00c8b3));
        REQUIRE (settings.getSendColour (0)
                 == LooperSettings::defaultSendColour (0));

        // Ungültiger Index fällt sauber zurück (kein UB)
        REQUIRE (settings.getSendColour (9) == LooperSettings::defaultSendColour (0));
        settings.setSendColour (9, juce::Colours::red);   // No-op

        // Eigene Farbe wird opak gespeichert (halbtransparent → voll)
        settings.setSendColour (1, juce::Colour (0x80123456));
        REQUIRE (settings.getSendColour (1) == juce::Colour (0xff123456));
        settings.flush();
    }

    LooperSettings reloaded { temp.options() };
    REQUIRE (reloaded.getSendColour (1) == juce::Colour (0xff123456));
    REQUIRE (reloaded.getSendColour (0) == juce::Colour (0xffff8d28));   // unberührt
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
