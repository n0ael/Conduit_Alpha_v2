#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "RelativeEncoding.h"

namespace conduit::midirig
{

//==============================================================================
/** Send-/Feedback-Adressen eines Controller-Profils (ADR 006 E2) koennen
    CC, Note ODER Pitch Bend sein -- anders als die Klangerzeuger-Profile
    (MidiDeviceProfile.h), die praktisch immer CC/NRPN sind, senden Pads/
    Taster meist Noten; Motorfader/Ribbons (M8, AlphaTrack) senden Pitch
    Bend. Bei pitchBend ist der KANAL die Adresse (kein number) -- der
    Fader des AlphaTrack lebt auf ch1, der Touch-Strip auf ch10. */
enum class AddressKind { cc, note, pitchBend };

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
    int sendNumber  = -1;   // -1 = nicht belegt (pitchBend: immer 0)

    /** M8: EINGANGS-Modus des Controls. Leer = absolut (Default, Soft-
        Takeover greift). `scrub` = absoluter Positions-Strom wird RELATIV
        angewendet (Delta zur letzten Position, Ribbon/Touch-Strip --
        User-Entscheidung 16.07.2026); `relative` = signed Ticks
        (Endlos-Encoder, Kodierung 1..63 = +, 65..127 = -). */
    juce::String mode;

    /** M8: nur mode=relative -- Ticks fuer den vollen Regelweg (0 = Default,
        siehe MidiInBindings::kDefaultRelativeSteps). */
    int steps = 0;

    /** M8: nur mode=relative -- WIE der Encoder sein Delta kodiert (CSV-Spalte
        `rel_encoding`). Geraeteabhaengig, siehe RelativeEncoding.h; Default =
        Zweierkomplement (Profile ohne die Spalte verhalten sich wie M7). */
    RelativeEncoding relEncoding = RelativeEncoding::twosComplement;

    /** M8: Touch-Note dieses Controls (AlphaTrack: Fader 0x68, Strip 0x74,
        Encoder 0x78..0x7a), -1 = keine. Touch-Noten sind KEINE Bindungs-
        Quellen (Learn-Falle: der Griff zum Fader wuerde sonst die Note
        binden) -- GridPage filtert sie und nutzt sie als Motor-Gate. */
    int touchNumber = -1;

    std::vector<FeedbackAddress> feedback;   // 0..3 Eintraege
};

/** M7: `role`-Wert des Channelstrip-Ebenen-Selektors. */
inline constexpr const char* kRoleLayerSelect = "layer_select";

/** M8: `mode`-Werte (Spalte `mode`, s. ControllerControl::mode). */
inline constexpr const char* kModeScrub    = "scrub";
inline constexpr const char* kModeRelative = "relative";

/** M8: `type`-Wert fuer reine Touch-Sensoren ohne Eigenfunktion (z. B. die
    2-Finger-Note des AlphaTrack-Strips): die Send-Note wird wie eine
    Touch-Note gefiltert, das Control traegt nie eine Bindung. */
inline constexpr const char* kTypeTouch = "touch";

//==============================================================================
/** Controller-Profil (ADR 006 E2) -- EIN Geraet, im Gegensatz zu den
    Klangerzeuger-CSVs (MidiDeviceProfile.h) traegt eine Controller-CSV nie
    mehrere Geraete (die Ordnerkonvention `Conduit/Controllers/{Geraet}.csv`
    ist bereits pro Geraet benannt). */
struct ControllerProfile
{
    juce::String device;

    /** Live-Remote-Bridge (07/2026): Display-FAEHIGKEIT des Geraets --
        Treiber-Wahl datengetrieben statt Geraetename-Switch (Rule midirig).
        Bekannte Werte: "alphatrack_lcd" (2x16 via SysEx). Leer = kein
        Display. CSV-Spalte `display` (erste nicht-leere Zelle gewinnt,
        Muster `device`); fehlende Spalte -> leer (Alt-CSVs unveraendert). */
    juce::String display;

    std::vector<ControllerControl> controls;

    /** Erstes Control, dessen Send-Adresse zu (kind, number) passt -- Kern
        der Laufzeit-Zuordnung fuer MidiInBindings::onFeedbackEcho (welches
        physische Control hat den angewendeten CC/Note gesendet). BEWUSST
        kanal-agnostisch (M4b, User-Entscheidung 14.07.2026): der Kanal ist
        Geraete-Eigenschaft (`RigDevice::midiChannel`), nicht Profil-
        Eigenschaft -- die Kanal-Spalten im CSV werden geparst, aber beim
        Matching ignoriert. AUSNAHME pitchBend (M8): dort IST der Kanal die
        Adresse -- `number` traegt dann den Send-KANAL (1..16), gematcht
        wird gegen `sendChannel`. Pure, nullptr bei keinem Treffer. */
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

    Spalten: id, type, section, group, role, mode, steps, rel_encoding,
    touch_number, send_kind, send_channel, send_number,
    feedback1_kind, feedback1_channel, feedback1_number, feedback1_meaning,
    feedback2_kind, feedback2_channel, feedback2_number, feedback2_meaning,
    feedback3_kind, feedback3_channel, feedback3_number, feedback3_meaning.
    `group` (M6) und `mode`/`steps`/`rel_encoding`/`touch_number` (M8) sind
    optional -- CSVs ohne die Spalten parsen unveraendert.
    `*_kind`-Spalten: "note" fuer Noten-Adressen, "pitchbend" (auch "pb")
    fuer Pitch Bend (M8), alles andere (auch leer) = CC (Default). Zeilen
    ohne `id` ODER ohne `send_number` werden uebersprungen + gezaehlt
    (Ausnahme: pitchBend braucht keine send_number -- sie wird 0). `device` kommt aus einer eigenen Spalte
    "device" (erste nicht-leere Zeile gewinnt) -- fehlt sie, bleibt
    `ControllerProfile::device` leer (die Library setzt dann den
    Dateinamen als Fallback, siehe ControllerProfileLibrary). */
[[nodiscard]] ControllerProfile parseControllerProfileCsv (
    const juce::String& text, ControllerParseReport* report = nullptr);

} // namespace conduit::midirig
