#include <catch2/catch_test_macros.hpp>

#include "UI/LooperPage.h"

//==============================================================================
TEST_CASE ("LooperPage: Quellen-Liste, persistierte Auswahl und Klick-Callback", "[looper][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::LooperPage page;
    page.setBounds (0, 0, 900, 500);

    const std::vector<conduit::LooperPage::Source> sources = {
        { "master", "Master" },
        { "hw:0",   "In 1 / In 2" },
        { "tap:delay_1", "Tap: delay_1" },
    };

    juce::String selectedKey;
    page.onSourceSelected = [&] (const juce::String& key) { selectedKey = key; };

    SECTION ("persistierter Schlüssel wird vorausgewählt — ohne Callback")
    {
        page.setSources (sources, "tap:delay_1");
        REQUIRE (page.getSourceCombo().getNumItems() == 3);
        REQUIRE (page.getSourceCombo().getSelectedItemIndex() == 2);
        REQUIRE (selectedKey.isEmpty());  // Anzeige ≠ User-Klick
    }

    SECTION ("unbekannter Schlüssel fällt auf die erste Quelle (Master)")
    {
        page.setSources (sources, "hw:99");
        REQUIRE (page.getSourceCombo().getSelectedItemIndex() == 0);
        REQUIRE (selectedKey.isEmpty());
    }

    SECTION ("User-Auswahl liefert den Quell-Schlüssel")
    {
        page.setSources (sources, "master");

        page.getSourceCombo().setSelectedItemIndex (1, juce::sendNotificationSync);
        REQUIRE (selectedKey == "hw:0");
    }

    SECTION ("Output-Paare (B6): Anker vorausgewählt, OOB geclampt, Klick meldet Index")
    {
        const juce::StringArray pairs { "Out 1 / Out 2", "Out 3 / Out 4" };
        int selectedPair = -1;
        page.onOutputPairSelected = [&] (int pair) { selectedPair = pair; };

        page.setOutputPairs (pairs, 1);
        REQUIRE (page.getOutputCombo().getNumItems() == 2);
        REQUIRE (page.getOutputCombo().getSelectedItemIndex() == 1);
        REQUIRE (selectedPair == -1);  // Anzeige ≠ User-Klick

        // Gerätewechsel: persistierter Anker jenseits der Paar-Liste
        page.setOutputPairs (pairs, 7);
        REQUIRE (page.getOutputCombo().getSelectedItemIndex() == 1);
        REQUIRE (selectedPair == -1);

        page.getOutputCombo().setSelectedItemIndex (0, juce::sendNotificationSync);
        REQUIRE (selectedPair == 0);
    }

    SECTION ("Stop-Kachel: initial disabled (kein Loop), Klick meldet onStop")
    {
        REQUIRE_FALSE (page.getStopTile().isEnabled());

        bool stopped = false;
        page.onStop = [&] { stopped = true; };
        page.getStopTile().setEnabled (true);  // Editor-Timer bei laufendem Loop
        page.getStopTile().onClick();          // synchron (kein Modal-Loop verfügbar)
        REQUIRE (stopped);
    }
}
