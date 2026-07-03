#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/UiSettings.h"

using Catch::Approx;
using conduit::UiSettings;

namespace
{

/** Persistenz in ein Temp-Verzeichnis statt in die echte Settings-Datei
    (Muster TempMeterSettings). */
struct TempUiSettings
{
    TempUiSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitUiSettingsTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();
    }

    ~TempUiSettings() { folder.deleteRecursively(); }

    [[nodiscard]] juce::PropertiesFile::Options options() const
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "ConduitUiSettingsTests";
        o.filenameSuffix  = ".settings";
        o.folderName      = folder.getFullPathName();  // absoluter Pfad
        return o;
    }

    juce::File folder;
};

} // namespace

//==============================================================================
TEST_CASE ("UiSettings: Defaults — Scale 1.0, FontScale 1.0, Dev Mode aus", "[uisettings][io]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempUiSettings temp;
    UiSettings settings (temp.options());

    REQUIRE (settings.getUiScale() == Approx (1.0f));
    REQUIRE (settings.getFontScale() == Approx (1.0f));
    REQUIRE_FALSE (settings.isDevModeEnabled());
}

TEST_CASE ("UiSettings: Setter clampen auf die erlaubten Bereiche", "[uisettings][io]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempUiSettings temp;
    UiSettings settings (temp.options());

    settings.setUiScale (3.0f);
    REQUIRE (settings.getUiScale() == Approx (UiSettings::maxUiScale));
    settings.setUiScale (0.1f);
    REQUIRE (settings.getUiScale() == Approx (UiSettings::minUiScale));

    settings.setFontScale (2.0f);
    REQUIRE (settings.getFontScale() == Approx (UiSettings::maxFontScale));
    settings.setFontScale (0.0f);
    REQUIRE (settings.getFontScale() == Approx (UiSettings::minFontScale));
}

TEST_CASE ("UiSettings: Werte überstehen den Roundtrip", "[uisettings][io]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempUiSettings temp;

    {
        UiSettings settings (temp.options());
        settings.setUiScale (1.5f);
        settings.setFontScale (1.2f);
        settings.setDevModeEnabled (true);
    }

    UiSettings reloaded (temp.options());
    REQUIRE (reloaded.getUiScale() == Approx (1.5f));
    REQUIRE (reloaded.getFontScale() == Approx (1.2f));
    REQUIRE (reloaded.isDevModeEnabled());
}

TEST_CASE ("UiSettings: defekte Datei wird beim Laden geclamped", "[uisettings][io]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempUiSettings temp;

    // Datei mit absurden Werten vorbereiten (handeditiert/defekt)
    {
        juce::PropertiesFile file (temp.options());
        file.setValue ("uiScale", 99.0);
        file.setValue ("fontScale", 0.01);
        file.save();
    }

    UiSettings settings (temp.options());
    REQUIRE (settings.getUiScale() == Approx (UiSettings::maxUiScale));
    REQUIRE (settings.getFontScale() == Approx (UiSettings::minFontScale));
}

TEST_CASE ("UiSettings: ChangeBroadcast nur bei echter Änderung", "[uisettings][io]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempUiSettings temp;
    UiSettings settings (temp.options());

    struct Counter : juce::ChangeListener
    {
        int calls = 0;
        void changeListenerCallback (juce::ChangeBroadcaster*) override { ++calls; }
    } counter;

    settings.addChangeListener (&counter);

    // Pro Block dispatchen — der ChangeBroadcaster koalesziert mehrere
    // sendChangeMessage vor der Zustellung zu EINEM Callback
    settings.setUiScale (1.5f);            // Änderung → Broadcast
    settings.setUiScale (1.5f);            // no-op (exactlyEqual) → kein Broadcast
    settings.dispatchPendingMessages();

    settings.setDevModeEnabled (true);     // Änderung → Broadcast
    settings.setDevModeEnabled (true);     // no-op
    settings.setFontScale (1.0f);          // no-op (Default) → kein Broadcast
    settings.dispatchPendingMessages();

    settings.removeChangeListener (&counter);
    REQUIRE (counter.calls == 2);
}
