#pragma once

#include <vector>

#include <juce_core/juce_core.h>

namespace conduit::midirig
{

//==============================================================================
/** Send-/Feedback-Adressen eines Controller-Profils (ADR 006 E2) koennen
    CC ODER Note sein -- anders als die Klangerzeuger-Profile (MidiDeviceProfile.h),
    die praktisch immer CC/NRPN sind, senden Pads/Taster meist Noten. */
enum class AddressKind { cc, note };

/** Eine Feedback-Adresse ("was schickt Conduit zur Rueckmeldung an dieses
    physische Control zurueck"): LED-Ring, Motorfader-Position, Display-
    Text (Sende-Weg fuer Display-Text existiert erst mit M8/SysEx-Snippets --
    die Laufzeit ueberspringt `meaning == "display"` bewusst still, ADR E6). */
struct FeedbackAddress
{
    AddressKind kind = AddressKind::cc;
    int channel = 1;      // 1..16
    int number  = -1;     // CC-Nummer bzw. Notennummer, -1 = nicht belegt
    juce::String meaning;  // z. B. "led_ring", "motor_fader", "display", frei
};

/** Ein physisches Control eines Controller-Profils (ADR 006 E2): id +
    Typ (frei, ueblich knob|fader|pad|encoder) + die Adresse, die es beim
    Anfassen SENDET, + bis zu 3 Feedback-Adressen. */
struct ControllerControl
{
    juce::String id;
    juce::String type;
    juce::String section;   // optionale Gruppierung (Anzeige), leer erlaubt

    /** M6: optionale FUNKTIONS-Gruppe (z. B. "col1" fuer eine K1-Spalte) --
        anders als `section` (reine Anzeige) traegt sie Laufzeit-Semantik:
        Controls einer Gruppe teilen sich eine Status-LED (Feedback-meanings
        status_red/status_amber/status_green am Status-Control der Gruppe),
        deren Push die Detail-Anzeige der Gruppe aktiviert. Leer = keine. */
    juce::String group;

    /** M7: optionale Laufzeit-ROLLE. `layer_select` (siehe kRoleLayerSelect)
        macht dieses Control zum Channelstrip-Ebenen-Selektor seiner `group`:
        Drehen waehlt eine von 3 Binding-Baenken fuer alle uebrigen Controls
        der Spalte, statt selbst eine Bindung zu tragen. Leer = normale
        (ggf. geebente) Bindung. */
    juce::String role;

    AddressKind sendKind = AddressKind::cc;
    int sendChannel = 1;
    int sendNumber  = -1;   // -1 = nicht belegt

    std::vector<FeedbackAddress> feedback;   // 0..3 Eintraege
};

/** M7: `role`-Wert des Channelstrip-Ebenen-Selektors. */
inline constexpr const char* kRoleLayerSelect = "layer_select";

//==============================================================================
/** Controller-Profil (ADR 006 E2) -- EIN Geraet, im Gegensatz zu den
    Klangerzeuger-CSVs (MidiDeviceProfile.h) traegt eine Controller-CSV nie
    mehrere Geraete (die Ordnerkonvention `Conduit/Controllers/{Geraet}.csv`
    ist bereits pro Geraet benannt). */
struct ControllerProfile
{
    juce::String device;
    std::vector<ControllerControl> controls;

    /** Erstes Control, dessen Send-Adresse zu (kind, number) passt -- Kern
        der Laufzeit-Zuordnung fuer MidiInBindings::onFeedbackEcho (welches
        physische Control hat den angewendeten CC/Note gesendet). BEWUSST
        kanal-agnostisch (M4b, User-Entscheidung 14.07.2026): der Kanal ist
        Geraete-Eigenschaft (`RigDevice::midiChannel`), nicht Profil-
        Eigenschaft -- die Kanal-Spalten im CSV werden geparst, aber beim
        Matching ignoriert. Pure, nullptr bei keinem Treffer. */
    [[nodiscard]] const ControllerControl* findBySendAddress (
        AddressKind kind, int number) const noexcept;
};

//==============================================================================
/** Ergebnis eines Parse-Laufs (ADR E1b-Prinzip: kein stilles Scheitern --
    die UI zeigt den Report, Muster MidiDeviceProfile.h::ParseReport). */
struct ControllerParseReport
{
    int accepted = 0;
    int skipped  = 0;
    juce::StringArray warnings;
};

/** Toleranter CSV-Parser fuer "Conduit Controller Profile v1" (ADR 006 E2,
    pure, Muster MidiDeviceProfile.h::parseMidiGuideCsv): Header-Zeilen-
    getrieben, Spalten ueber ihre Namen (case-insensitiv), unbekannte
    Spalten ignoriert, RFC-4180-Quoting unterstuetzt.

    Spalten: id, type, section, group, role, send_kind, send_channel, send_number,
    feedback1_kind, feedback1_channel, feedback1_number, feedback1_meaning,
    feedback2_kind, feedback2_channel, feedback2_number, feedback2_meaning,
    feedback3_kind, feedback3_channel, feedback3_number, feedback3_meaning.
    `group` ist optional (M6) -- CSVs ohne die Spalte parsen unveraendert.
    `*_kind`-Spalten: "note" fuer Noten-Adressen, alles andere (auch leer)
    = CC (Default). Zeilen ohne `id` ODER ohne `send_number` werden
    uebersprungen + gezaehlt. `device` kommt aus einer eigenen Spalte
    "device" (erste nicht-leere Zeile gewinnt) -- fehlt sie, bleibt
    `ControllerProfile::device` leer (die Library setzt dann den
    Dateinamen als Fallback, siehe ControllerProfileLibrary). */
[[nodiscard]] ControllerProfile parseControllerProfileCsv (
    const juce::String& text, ControllerParseReport* report = nullptr);

} // namespace conduit::midirig
