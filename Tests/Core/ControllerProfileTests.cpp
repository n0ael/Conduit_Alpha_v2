#include <catch2/catch_test_macros.hpp>

#include "Core/ControllerProfile.h"

using namespace conduit::midirig;

//==============================================================================
TEST_CASE ("ControllerProfile: CSV-Parsing liest id/type/send + Feedback-Adressen", "[midirig]")
{
    const auto text = juce::String (
        "device,id,type,section,send_kind,send_channel,send_number,"
        "feedback1_kind,feedback1_channel,feedback1_number,feedback1_meaning\n"
        "Xone:K1,enc1,knob,Row A,cc,1,0,cc,1,0,led_ring\n");

    ControllerParseReport report;
    const auto profile = parseControllerProfileCsv (text, &report);

    CHECK (report.accepted == 1);
    CHECK (report.skipped == 0);
    REQUIRE (profile.device == "Xone:K1");
    REQUIRE (profile.controls.size() == 1);

    const auto& control = profile.controls[0];
    CHECK (control.id == "enc1");
    CHECK (control.type == "knob");
    CHECK (control.section == "Row A");
    CHECK (control.sendKind == AddressKind::cc);
    CHECK (control.sendChannel == 1);
    CHECK (control.sendNumber == 0);
    REQUIRE (control.feedback.size() == 1);
    CHECK (control.feedback[0].kind == AddressKind::cc);
    CHECK (control.feedback[0].number == 0);
    CHECK (control.feedback[0].meaning == "led_ring");
}

TEST_CASE ("ControllerProfile: Spalten in beliebiger Reihenfolge, unbekannte ignoriert", "[midirig]")
{
    const auto text = juce::String (
        "send_number,unbekannt,id,send_kind\n"
        "16,egal,fader1,cc\n");

    const auto profile = parseControllerProfileCsv (text);

    REQUIRE (profile.controls.size() == 1);
    CHECK (profile.controls[0].sendNumber == 16);
    CHECK (profile.controls[0].sendChannel == 1);   // Default, Spalte fehlt
}

TEST_CASE ("ControllerProfile: Note-Adressen (Pads)", "[midirig]")
{
    const auto text = juce::String (
        "id,send_kind,send_number,feedback1_kind,feedback1_number,feedback1_meaning,"
        "feedback2_kind,feedback2_number,feedback2_meaning\n"
        "btn_c3,note,60,note,84,led_layer_b,note,108,led_layer_c\n");

    const auto profile = parseControllerProfileCsv (text);

    REQUIRE (profile.controls.size() == 1);
    const auto& control = profile.controls[0];
    CHECK (control.sendKind == AddressKind::note);
    CHECK (control.sendNumber == 60);
    REQUIRE (control.feedback.size() == 2);
    CHECK (control.feedback[0].meaning == "led_layer_b");
    CHECK (control.feedback[1].meaning == "led_layer_c");
}

TEST_CASE ("ControllerProfile: bis zu 3 Feedback-Adressen, 0 sind gueltig", "[midirig]")
{
    const auto text = juce::String ("id,send_number\nfader1,16\n");
    const auto profile = parseControllerProfileCsv (text);

    REQUIRE (profile.controls.size() == 1);
    CHECK (profile.controls[0].feedback.empty());
}

TEST_CASE ("ControllerProfile: Zeilen ohne id oder ohne send_number uebersprungen", "[midirig]")
{
    const auto text = juce::String (
        "id,send_number\n"
        ",5\n"
        "orphan,\n"
        "valid,7\n");

    ControllerParseReport report;
    const auto profile = parseControllerProfileCsv (text, &report);

    REQUIRE (profile.controls.size() == 1);
    CHECK (profile.controls[0].id == "valid");
    CHECK (report.accepted == 1);
    CHECK (report.skipped == 2);
}

TEST_CASE ("ControllerProfile: kaputte Kopfzeile -- kein Crash, ein Warning", "[midirig]")
{
    ControllerParseReport report;
    const auto profile = parseControllerProfileCsv ("nur,ein,paar,spalten\nohne,sinn\n", &report);

    CHECK (profile.controls.empty());
    CHECK_FALSE (report.warnings.isEmpty());
}

TEST_CASE ("ControllerProfile: leerer Text -- kein Crash", "[midirig]")
{
    const auto profile = parseControllerProfileCsv ({});
    CHECK (profile.controls.empty());
}

TEST_CASE ("ControllerProfile: group-Spalte (M6) -- optional, Status-meanings roundtrippen", "[midirig]")
{
    // M6: `group` bindet Controls an eine Status-LED-Gruppe; die neuen
    // meanings (status_red/status_amber/status_green, led_pickup) sind
    // freie Strings und muessen den Parser unveraendert ueberstehen.
    const auto text = juce::String (
        "id,type,group,send_kind,send_number,"
        "feedback1_kind,feedback1_number,feedback1_meaning,"
        "feedback2_kind,feedback2_number,feedback2_meaning,"
        "feedback3_kind,feedback3_number,feedback3_meaning\n"
        "enc_r1_1_push,pad,col1,note,52,note,52,status_red,note,88,status_amber,note,124,status_green\n"
        "fader1,fader,col1,cc,16,note,24,led_pickup,,,,,,\n");

    const auto profile = parseControllerProfileCsv (text);
    REQUIRE (profile.controls.size() == 2);

    const auto& status = profile.controls[0];
    CHECK (status.group == "col1");
    REQUIRE (status.feedback.size() == 3);
    CHECK (status.feedback[0].meaning == "status_red");
    CHECK (status.feedback[1].meaning == "status_amber");
    CHECK (status.feedback[2].meaning == "status_green");

    const auto& fader = profile.controls[1];
    CHECK (fader.group == "col1");
    REQUIRE (fader.feedback.size() == 1);
    CHECK (fader.feedback[0].kind == AddressKind::note);
    CHECK (fader.feedback[0].number == 24);
    CHECK (fader.feedback[0].meaning == "led_pickup");
}

TEST_CASE ("ControllerProfile: CSV ohne group-Spalte parst unveraendert (Bestand)", "[midirig]")
{
    const auto profile = parseControllerProfileCsv ("id,send_number\nfader1,16\n");
    REQUIRE (profile.controls.size() == 1);
    CHECK (profile.controls[0].group.isEmpty());
}

//==============================================================================
TEST_CASE ("ControllerProfile::findBySendAddress: kanal-agnostisch, Kind/Nummer trennen", "[midirig]")
{
    // M4b: der CSV-Kanal wird beim Matching IGNORIERT (Kanal ist Geraete-
    // Eigenschaft, RigDevice::midiChannel) -- enc1 auf CSV-Kanal 5 wird
    // trotzdem gefunden.
    const auto text = juce::String (
        "id,send_kind,send_channel,send_number\n"
        "enc1,cc,5,0\n"
        "pad1,note,5,0\n");

    const auto profile = parseControllerProfileCsv (text);
    REQUIRE (profile.controls.size() == 2);

    const auto* cc = profile.findBySendAddress (AddressKind::cc, 0);
    REQUIRE (cc != nullptr);
    CHECK (cc->id == "enc1");

    // Gleiche Nummer, anderer Kind: Note 0 trifft pad1, nicht enc1.
    const auto* note = profile.findBySendAddress (AddressKind::note, 0);
    REQUIRE (note != nullptr);
    CHECK (note->id == "pad1");

    CHECK (profile.findBySendAddress (AddressKind::cc, 99) == nullptr);
}

TEST_CASE ("M8: rel_encoding wird geparst (fehlende Spalte = Zweierkomplement)", "[midirig][m8]")
{
    const auto csv = juce::String (
        "device,id,type,mode,steps,rel_encoding,send_kind,send_channel,send_number\n"
        "T,enc_sign,encoder,relative,127,signbit,cc,1,16\n"
        "T,enc_bin,encoder,relative,64,binoffset,cc,1,17\n"
        "T,enc_default,encoder,relative,,,cc,1,18\n");

    ControllerParseReport report;
    const auto profile = parseControllerProfileCsv (csv, &report);
    REQUIRE (report.accepted == 3);

    CHECK (profile.findBySendAddress (AddressKind::cc, 16)->relEncoding == RelativeEncoding::signBit);
    CHECK (profile.findBySendAddress (AddressKind::cc, 17)->relEncoding == RelativeEncoding::binaryOffset);
    CHECK (profile.findBySendAddress (AddressKind::cc, 18)->relEncoding == RelativeEncoding::twosComplement);
}

TEST_CASE ("M8: CSV ohne rel_encoding-Spalte bleibt Zweierkomplement (M7-Kompatibilitaet)", "[midirig][m8]")
{
    const auto csv = juce::String (
        "device,id,type,mode,send_kind,send_channel,send_number\n"
        "T,enc,encoder,relative,cc,1,0\n");

    const auto profile = parseControllerProfileCsv (csv);
    CHECK (profile.findBySendAddress (AddressKind::cc, 0)->relEncoding == RelativeEncoding::twosComplement);
}
