#include <catch2/catch_test_macros.hpp>

#include "Core/ChannelNames.h"

using conduit::ChannelNames;
using Direction = conduit::ChannelNames::Direction;

namespace
{

/** Persistenz in ein Temp-Verzeichnis statt in die echte Settings-Datei. */
struct TempSettingsFolder
{
    TempSettingsFolder()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitChannelNamesTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();
    }

    ~TempSettingsFolder() { folder.deleteRecursively(); }

    [[nodiscard]] juce::PropertiesFile::Options options() const
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "ConduitChannelNamesTests";
        o.filenameSuffix  = ".settings";
        o.folderName      = folder.getFullPathName();  // absoluter Pfad
        return o;
    }

    juce::File folder;
};

} // namespace

//==============================================================================
TEST_CASE ("ChannelNames: Default-Fallbacks ohne Eintrag", "[channelnames]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettingsFolder temp;
    ChannelNames names (temp.options());

    SECTION ("Ohne Device-Kontext: In N / Out N")
    {
        REQUIRE (names.getLabel (Direction::input, 0)  == "In 1");
        REQUIRE (names.getLabel (Direction::output, 3) == "Out 4");
    }

    SECTION ("Gemeldeter Kanalname des Devices hat Vorrang vor In N")
    {
        names.setActiveDevice ("ES-3", { "ADAT 1", "ADAT 2" }, { "Main L" });

        REQUIRE (names.getLabel (Direction::input, 0)  == "ADAT 1");
        REQUIRE (names.getLabel (Direction::input, 1)  == "ADAT 2");
        REQUIRE (names.getLabel (Direction::output, 0) == "Main L");

        // Außerhalb der gemeldeten Liste: Index-Fallback
        REQUIRE (names.getLabel (Direction::input, 2)  == "In 3");
        REQUIRE (names.getLabel (Direction::output, 1) == "Out 2");
    }

    SECTION ("Leerer gemeldeter Name fällt auf In N zurück")
    {
        names.setActiveDevice ("ES-3", { "" }, {});
        REQUIRE (names.getLabel (Direction::input, 0) == "In 1");
    }
}

//==============================================================================
TEST_CASE ("ChannelNames: userLabel-Overrides und Richtungs-Trennung", "[channelnames]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettingsFolder temp;
    ChannelNames names (temp.options());
    names.setActiveDevice ("ES-3", { "ADAT 1" }, {});

    SECTION ("Override gewinnt gegen gemeldeten Namen")
    {
        names.setUserLabel (Direction::input, 0, "Kick");
        REQUIRE (names.getLabel (Direction::input, 0) == "Kick");
        REQUIRE (names.getUserLabel (Direction::input, 0) == "Kick");
    }

    SECTION ("Input und Output sind getrennte Keys")
    {
        names.setUserLabel (Direction::input, 0, "Kick");
        REQUIRE (names.getUserLabel (Direction::output, 0).isEmpty());
        REQUIRE (names.getLabel (Direction::output, 0) == "Out 1");
    }

    SECTION ("Leere Eingabe löscht den Eintrag — zurück zum Default")
    {
        names.setUserLabel (Direction::input, 0, "Kick");
        names.setUserLabel (Direction::input, 0, "   ");
        REQUIRE (names.getUserLabel (Direction::input, 0).isEmpty());
        REQUIRE (names.getLabel (Direction::input, 0) == "ADAT 1");
    }

    SECTION ("Label wird getrimmt und gekürzt")
    {
        names.setUserLabel (Direction::input, 0, "  Kick  ");
        REQUIRE (names.getUserLabel (Direction::input, 0) == "Kick");

        names.setUserLabel (Direction::input, 0, juce::String::repeatedString ("x", 200));
        REQUIRE (names.getUserLabel (Direction::input, 0).length()
                 == ChannelNames::maxLabelLength);
    }

    SECTION ("Ohne aktives Device ist setUserLabel ein No-op")
    {
        ChannelNames detached (temp.options());
        detached.setUserLabel (Direction::input, 0, "Kick");
        REQUIRE (detached.getUserLabel (Direction::input, 0).isEmpty());
    }
}

//==============================================================================
TEST_CASE ("ChannelNames: Device-Matching exakt / Prefix / kein Match (8.1)", "[channelnames]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettingsFolder temp;
    ChannelNames names (temp.options());

    names.setActiveDevice ("ES-3", {}, {});
    names.setUserLabel (Direction::input, 0, "Kick");

    SECTION ("Exakter Match")
    {
        names.setActiveDevice ("ES-3", {}, {});
        REQUIRE (names.getLabel (Direction::input, 0) == "Kick");
    }

    SECTION ("Prefix-Match: aktives Device trägt Suffix")
    {
        names.setActiveDevice ("ES-3 (2)", {}, {});
        REQUIRE (names.getLabel (Direction::input, 0) == "Kick");
    }

    SECTION ("Prefix-Match: gespeicherter Key trägt Suffix")
    {
        ChannelNames fresh (temp.options());
        fresh.setActiveDevice ("ES-3 (2)", {}, {});
        fresh.setUserLabel (Direction::input, 1, "Snare");

        fresh.setActiveDevice ("ES-3", {}, {});
        REQUIRE (fresh.getLabel (Direction::input, 1) == "Snare");
    }

    SECTION ("Kein Match → Default, fremdes Profil bleibt unberührt")
    {
        names.setActiveDevice ("Babyface", {}, {});
        REQUIRE (names.getLabel (Direction::input, 0) == "In 1");

        names.setActiveDevice ("ES-3", {}, {});
        REQUIRE (names.getLabel (Direction::input, 0) == "Kick");
    }

    SECTION ("Schreiben bei Prefix-Match aktualisiert das bestehende Profil")
    {
        names.setActiveDevice ("ES-3 (2)", {}, {});
        names.setUserLabel (Direction::input, 0, "Bassdrum");

        names.setActiveDevice ("ES-3", {}, {});
        REQUIRE (names.getLabel (Direction::input, 0) == "Bassdrum");
    }
}

//==============================================================================
TEST_CASE ("ChannelNames: stripDeviceSuffix", "[channelnames]")
{
    REQUIRE (ChannelNames::stripDeviceSuffix ("ES-3 (2)")  == "ES-3");
    REQUIRE (ChannelNames::stripDeviceSuffix ("ES-3 (12)") == "ES-3");
    REQUIRE (ChannelNames::stripDeviceSuffix ("ES-3")      == "ES-3");

    // Klammern ohne Zähler-Semantik bleiben Teil des Namens
    REQUIRE (ChannelNames::stripDeviceSuffix ("Scarlett (USB)") == "Scarlett (USB)");
    REQUIRE (ChannelNames::stripDeviceSuffix ("(2)")            == "(2)");
    REQUIRE (ChannelNames::stripDeviceSuffix ("ES-3 ()")        == "ES-3 ()");
}

//==============================================================================
TEST_CASE ("ChannelNames: sanitizeFileLabel für Dateinamen", "[channelnames]")
{
    SECTION ("Verbotene Zeichen werden ersetzt")
    {
        REQUIRE (ChannelNames::sanitizeFileLabel ("a/b\\c:d*e?f\"g<h>i|j", "x")
                 == "a_b_c_d_e_f_g_h_i_j");
        REQUIRE (ChannelNames::sanitizeFileLabel (juce::String ("tab\there"), "x")
                 == "tab_here");
    }

    SECTION ("Trim und Längen-Limit")
    {
        REQUIRE (ChannelNames::sanitizeFileLabel ("  Kick  ", "x") == "Kick");
        REQUIRE (ChannelNames::sanitizeFileLabel (juce::String::repeatedString ("y", 200), "x")
                     .length() == ChannelNames::maxLabelLength);
    }

    SECTION ("Leeres Ergebnis fällt auf den Fallback zurück")
    {
        REQUIRE (ChannelNames::sanitizeFileLabel ("", "in1")    == "in1");
        REQUIRE (ChannelNames::sanitizeFileLabel ("   ", "in1") == "in1");
    }
}

//==============================================================================
namespace
{
juce::BigInteger channelMask (std::initializer_list<int> channels)
{
    juce::BigInteger mask;
    for (const auto ch : channels)
        mask.setBit (ch);
    return mask;
}
}

TEST_CASE ("ChannelNames: aktive Teil-Auswahl mappt Port → Geräte-Kanal", "[channelnames][io]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettingsFolder temp;
    ChannelNames names (temp.options());

    // 4-Kanal-Device, aktiv nur Kanal 1 und 3 (0 und 2 deaktiviert) — der
    // AudioProcessorPlayer komprimiert: Port 0 → Kanal 1, Port 1 → Kanal 3
    names.setActiveDevice ("Interface", { "A", "B", "C", "D" }, {},
                           channelMask ({ 1, 3 }), {});

    SECTION ("Labels folgen den aktiven Kanälen, nicht dem Port-Index")
    {
        REQUIRE (names.getLabel (Direction::input, 0) == "B");  // Kanal 1
        REQUIRE (names.getLabel (Direction::input, 1) == "D");  // Kanal 3
    }

    SECTION ("User-Label bleibt am Geräte-Kanal verankert, wenn sich die Auswahl ändert")
    {
        names.setUserLabel (Direction::input, 0, "Kick");        // Port 0 = Kanal 1
        REQUIRE (names.getLabel (Direction::input, 0) == "Kick");

        // Kanal 0 zusätzlich aktivieren → Port 0 = Kanal 0, Port 1 = Kanal 1
        names.setActiveDevice ("Interface", { "A", "B", "C", "D" }, {},
                               channelMask ({ 0, 1, 3 }), {});

        REQUIRE (names.getLabel (Direction::input, 0) == "A");     // Kanal 0
        REQUIRE (names.getLabel (Direction::input, 1) == "Kick");  // Kick folgt Kanal 1
        REQUIRE (names.getLabel (Direction::input, 2) == "D");     // Kanal 3
    }

    SECTION ("Default-Fallback nutzt den echten Kanal (In N+1)")
    {
        // Kanal ohne gemeldeten Namen: aktiv nur Kanal 5 eines namenlosen Devices
        names.setActiveDevice ("Bare", {}, {}, channelMask ({ 5 }), {});
        REQUIRE (names.getLabel (Direction::input, 0) == "In 6");  // Kanal 5
    }

    SECTION ("Leere Maske → identisches Mapping (Rückwärtskompatibilität)")
    {
        names.setActiveDevice ("Interface", { "A", "B", "C", "D" }, {});
        REQUIRE (names.getLabel (Direction::input, 0) == "A");
        REQUIRE (names.getLabel (Direction::input, 2) == "C");
    }
}

//==============================================================================
TEST_CASE ("ChannelNames: Persistenz-Roundtrip", "[channelnames]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettingsFolder temp;

    {
        ChannelNames names (temp.options());
        names.setActiveDevice ("ES-3", {}, {});
        names.setUserLabel (Direction::input, 0, "Kick");
        names.setUserLabel (Direction::output, 2, "CV Pitch");
        names.setImagePath (Direction::input, 0, "C:/bilder/kick.png");
        names.flush();
    }

    ChannelNames reloaded (temp.options());
    reloaded.setActiveDevice ("ES-3", {}, {});

    REQUIRE (reloaded.getUserLabel (Direction::input, 0)  == "Kick");
    REQUIRE (reloaded.getUserLabel (Direction::output, 2) == "CV Pitch");
    REQUIRE (reloaded.getImagePath (Direction::input, 0)  == "C:/bilder/kick.png");

    // Gelöschte Labels überleben den Roundtrip nicht
    reloaded.setUserLabel (Direction::input, 0, "");
    reloaded.flush();

    ChannelNames again (temp.options());
    again.setActiveDevice ("ES-3", {}, {});
    REQUIRE (again.getUserLabel (Direction::input, 0).isEmpty());
    REQUIRE (again.getUserLabel (Direction::output, 2) == "CV Pitch");
}

//==============================================================================
TEST_CASE ("ChannelNames: Stereo-Pairing — Anker, Konfliktregel, Persistenz", "[channelnames][pairing]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettingsFolder temp;

    SECTION ("Koppeln/Lösen am Port, Anker am physischen Kanal")
    {
        ChannelNames names (temp.options());
        names.setActiveDevice ("ES-3", { "A", "B", "C", "D" }, {});

        REQUIRE_FALSE (names.isPortPairStart (Direction::input, 0));

        names.setPortPairedWithNext (Direction::input, 0, true);
        REQUIRE (names.isPortPairStart (Direction::input, 0));
        REQUIRE_FALSE (names.isPortPairStart (Direction::input, 1));  // Partner, kein Anker

        names.setPortPairedWithNext (Direction::input, 0, false);
        REQUIRE_FALSE (names.isPortPairStart (Direction::input, 0));
    }

    SECTION ("Konfliktregel: ein Kanal gehört zu höchstens einem Paar")
    {
        ChannelNames names (temp.options());
        names.setActiveDevice ("ES-3", { "A", "B", "C", "D" }, {});

        names.setPortPairedWithNext (Direction::input, 0, true);   // (0,1)
        names.setPortPairedWithNext (Direction::input, 1, true);   // (1,2) → löst (0,1)
        REQUIRE_FALSE (names.isPortPairStart (Direction::input, 0));
        REQUIRE (names.isPortPairStart (Direction::input, 1));

        names.setPortPairedWithNext (Direction::input, 2, true);   // (2,3) → löst (1,2)
        REQUIRE_FALSE (names.isPortPairStart (Direction::input, 1));
        REQUIRE (names.isPortPairStart (Direction::input, 2));

        // Richtungen sind getrennt — Output-Paar kollidiert nicht mit Input
        names.setActiveDevice ("ES-3", { "A", "B", "C", "D" }, { "L", "R" });
        names.setPortPairedWithNext (Direction::output, 0, true);
        REQUIRE (names.isPortPairStart (Direction::output, 0));
        REQUIRE (names.isPortPairStart (Direction::input, 2));
    }

    SECTION ("Teil-Auswahl: Paar verankert physisch, verschwindet bei Kanal-Lücke")
    {
        ChannelNames names (temp.options());

        // Alle vier Kanäle aktiv: Paar auf physisch (1,2) über Port 1 setzen
        juce::BigInteger all;
        all.setRange (0, 4, true);
        names.setActiveDevice ("ES-3", { "A", "B", "C", "D" }, {}, all);
        names.setPortPairedWithNext (Direction::input, 1, true);   // physisch (1,2)
        REQUIRE (names.isPortPairStart (Direction::input, 1));

        // Kanal 2 deaktiviert: Port 1 (physisch 1) und Port 2 (physisch 3)
        // sind keine physischen Nachbarn mehr → Paar wird nicht angezeigt …
        juce::BigInteger gap;
        gap.setBit (0);
        gap.setBit (1);
        gap.setBit (3);
        names.setActiveDevice ("ES-3", { "A", "B", "C", "D" }, {}, gap);
        REQUIRE_FALSE (names.isPortPairStart (Direction::input, 1));

        // … bleibt aber gespeichert und greift wieder mit allen Kanälen
        names.setActiveDevice ("ES-3", { "A", "B", "C", "D" }, {}, all);
        REQUIRE (names.isPortPairStart (Direction::input, 1));
    }

    SECTION ("Persistenz: Pairing überlebt den Roundtrip auch ohne Label")
    {
        {
            ChannelNames names (temp.options());
            names.setActiveDevice ("ES-3", {}, {});
            names.setPortPairedWithNext (Direction::input, 2, true);  // Flag-only-Entry
            names.flush();
        }

        ChannelNames reloaded (temp.options());
        reloaded.setActiveDevice ("ES-3", {}, {});
        REQUIRE (reloaded.isPortPairStart (Direction::input, 2));

        // Lösen räumt den Flag-only-Entry wieder aus der Datei
        reloaded.setPortPairedWithNext (Direction::input, 2, false);
        reloaded.flush();

        ChannelNames again (temp.options());
        again.setActiveDevice ("ES-3", {}, {});
        REQUIRE_FALSE (again.isPortPairStart (Direction::input, 2));
    }
}

//==============================================================================
TEST_CASE ("ChannelNames: Kanal-Farbe — set/get, Mapping, Prune, Persistenz", "[channelnames][colour]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettingsFolder temp;

    SECTION ("Default ist 0 (keine Farbe)")
    {
        ChannelNames names (temp.options());
        names.setActiveDevice ("ES-3", {}, {});
        REQUIRE (names.getColour (Direction::input, 0) == 0);
    }

    SECTION ("Setzen/Lesen, Richtungen getrennt, Löschen mit 0")
    {
        ChannelNames names (temp.options());
        names.setActiveDevice ("ES-3", { "A", "B" }, { "L" });

        names.setColour (Direction::input, 0, 0x00ff453au);
        REQUIRE (names.getColour (Direction::input, 0)  == 0x00ff453au);
        REQUIRE (names.getColour (Direction::output, 0) == 0);  // andere Richtung

        names.setColour (Direction::input, 0, 0);
        REQUIRE (names.getColour (Direction::input, 0) == 0);
    }

    SECTION ("Ohne aktives Device ist setColour ein No-op")
    {
        ChannelNames detached (temp.options());
        detached.setColour (Direction::input, 0, 0x0000bfd8u);
        REQUIRE (detached.getColour (Direction::input, 0) == 0);
    }

    SECTION ("Farbe folgt dem physischen Geräte-Kanal bei Auswahl-Änderung")
    {
        ChannelNames names (temp.options());
        // Aktiv nur Kanal 1 und 3: Port 0 → Kanal 1, Port 1 → Kanal 3
        names.setColour (Direction::input, 0, 0);  // no-op ohne Device
        names.setActiveDevice ("Interface", { "A", "B", "C", "D" }, {},
                               channelMask ({ 1, 3 }), {});
        names.setColour (Direction::input, 0, 0x003ddc84u);  // Port 0 = Kanal 1

        // Kanal 0 zusätzlich aktiv → Port 1 = Kanal 1
        names.setActiveDevice ("Interface", { "A", "B", "C", "D" }, {},
                               channelMask ({ 0, 1, 3 }), {});
        REQUIRE (names.getColour (Direction::input, 0) == 0);           // Kanal 0
        REQUIRE (names.getColour (Direction::input, 1) == 0x003ddc84u); // folgt Kanal 1
    }

    SECTION ("Prune: farb-only-Entry überlebt Roundtrip, 0 räumt ihn aus")
    {
        {
            ChannelNames names (temp.options());
            names.setActiveDevice ("ES-3", {}, {});
            names.setColour (Direction::input, 2, 0x00a066d3u);  // ohne Label/Pairing
            names.flush();
        }

        ChannelNames reloaded (temp.options());
        reloaded.setActiveDevice ("ES-3", {}, {});
        REQUIRE (reloaded.getColour (Direction::input, 2) == 0x00a066d3u);

        reloaded.setColour (Direction::input, 2, 0);
        reloaded.flush();

        ChannelNames again (temp.options());
        again.setActiveDevice ("ES-3", {}, {});
        REQUIRE (again.getColour (Direction::input, 2) == 0);
    }
}
