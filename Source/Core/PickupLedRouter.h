#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>

#include <juce_core/juce_core.h>

#include "ControllerProfile.h"
#include "MidiInBindings.h"

namespace conduit::midirig
{

//==============================================================================
/** Uebersetzt Pickup-Warte-Zustaende (MidiInBindings::onPickupStateChanged,
    pro Eingangs-Adresse) PROFIL-GETRIEBEN in Controller-LED-Sends (M6) --
    die universelle Schicht zwischen Takeover-Kern und Hardware: neue
    Controller bekommen Pickup-Feedback rein ueber CSV-Eintraege, ohne Code.

    Fuenf LED-Mechanismen (User-Entscheidungen 14./15.07.2026):

    1. GRUPPEN-STATUS: Controls mit gleichem `group` teilen sich eine
       Status-LED (das Control der Gruppe mit status_red/status_amber/
       status_green-Feedback). Aggregat: ROT = ein Fader der Gruppe wartet
       (dominiert), ORANGE = nur Knobs warten, GRUEN = nichts wartet und
       mindestens ein Member gebunden, AUS = Gruppe ungebunden. Dreht der
       User ein wartendes Control (activeRecently), BLINKT die Status-LED
       in der Member-Farbe distanz-kodiert (nah = schnell) und wird beim
       Abholen wieder solid.
    2. DETAIL-MODUS (momentary): solange der Push des Status-Controls
       gehalten wird, zeigen die `led_pickup`-Adressen der Gruppen-Member
       den Einzel-Status (gruen solid = abgeholt, rot distanz-blinkend =
       wartet, aus = ungebunden).
    3. EINFACHE PROFILE: ein `led_pickup` OHNE Gruppe blinkt immer, solange
       sein Control wartet.
    4. SHIFT-PAD-ANZEIGE (profil-unabhaengig): solange ein Modifier-Pad
       gehalten wird und die physische Position seiner Shift-Ebene bekannt
       ist, zeigt das Pad die RICHTUNG solid an (rot = Wert verringern,
       orange = Wert erhoehen, gruen = gefunden; kein Blinken). Den
       Naeherungswert (Blink) traegt die Spalten-Status-LED aus Mechanismus 1.
    5. EBENEN-BLINK (M7, flashLayer): nach jedem Schritt des Channelstrip-
       Ebenen-Encoders blinken alle Pads (type=="pad") der Spalte kurz schnell
       in der Ebenen-Farbe (0 rot / 1 gruen / 2 orange).

    Beim Verlassen jedes Blink-/Modus-Zustands wird der letzte Echo-Stand
    restauriert (lastEchoValueFor) -- blosses 0-Senden wuerde aktive
    Toggle-LEDs loeschen. Status-Adressen sind EXKLUSIV Router-gesteuert
    (der Echo-Pfad ueberspringt die meanings status_..., led_pickup und
    display).

    Headless, deterministisch ueber tick() (60-Hz-Hub-Drain) testbar.
    Message Thread. */
class PickupLedRouter
{
public:
    PickupLedRouter() = default;

    //==========================================================================
    // Seams (Besitzer: GridPage)

    /** LED-Send (isNote, nummer, value7bit) -- der Besitzer ergaenzt den
        Geraete-Kanal und baut die MidiMessage (Muster onFeedbackEcho). */
    std::function<void (bool isNote, int number, int value7bit)> send;

    /** Letzter Echo-Stand einer Feedback-Adresse (LED-Restore), -1 =
        unbekannt (dann wird 0 gesendet). */
    std::function<int (bool isNote, int number)> lastEchoValueFor;

    /** true, wenn irgendeine Bindung diese Send-Adresse traegt --
        kanal-agnostisch (M4b: Kanal ist Geraete-Eigenschaft). */
    std::function<bool (bool isNote, int number)> isAddressBound;

    //==========================================================================
    // Konfiguration

    /** Profil-KOPIE uebernehmen (Library-Reloads koennen den Speicher der
        Quelle invalidieren; Profile sind klein). */
    void setProfile (const ControllerProfile& profileToUse);
    void clearProfile();

    /** Zustand verwerfen OHNE Sends (Geraete-/Rollen-Wechsel: das alte
        Geraet ist weg, Restores liefen ins Leere). */
    void reset();

    //==========================================================================
    // Eingaenge [Message Thread]

    /** Von MidiInBindings::onPickupStateChanged durchgereicht. */
    void updatePickupState (const grid::InputAddress& address,
                            const grid::MidiInBindings::PickupState& state);

    /** Noten des Controller-Geraets (Status-Push-Beobachtung fuer den
        momentary Detail-Modus) -- rein passiv, die Note bleibt normal
        mapp-/lernbar. */
    void handleControllerNote (int noteNumber, bool isOn);

    /** M7b: aktive Ebene einer Spalte setzen (Encoder-Auswahl / Profil-Load).
        Alle Pads (type=="pad") der Spalte leuchten DAUERHAFT in der Ebenen-
        Farbe (0 rot / 1 gruen / 2 orange) als Basis-Anzeige; aktive Toggle-/
        Button-Pads blinken im 8tel-Takt (tempo-synchron). Wechselt die Ebene,
        flackert die ganze Spalte kurz (`kLayerChangeTicks`) im 16tel-Takt.
        Pickup-/Detail-/Shift-Anzeigen (Mechanismen 1-4) bleiben momentane
        Overrides ueber der Basis. */
    void setColumnLayer (const juce::String& column, int layer);

    /** M7b: aktuelle Beat-Position (LinkClock, Message Thread) fuer die
        tempo-synchronen 8tel-/16tel-Blinks. GridPage fuettert sie je Tick. */
    void setBeatPosition (double beat) noexcept { beatPosition = beat; }

    /** true, wenn der Router die LED (isNote, number) gerade aktiv bespielt
        (Shift-Richtung, Ebenen-Anzeige, Status ...). Der Echo-Pfad fragt das
        pro Feedback-Adresse ab und ueberspringt sie dann -- sonst
        ueberschreibt ein Wert-Echo die Router-Farbe und der Dedup verhindert
        die Korrektur (M6.1-Fund: rot/orange wurden vom Pad-Eigenfunktions-Echo
        auf dunkel gezogen). */
    [[nodiscard]] bool isManaging (bool isNote, int number) const noexcept
    {
        return lastSent.count (LedAddress { isNote, number }) > 0;
    }

    /** ~60 Hz (Hub-Tick, NACH midiInBindings.tick()): Blink-Phasen
        fortschreiben, Soll-Zustand diffen, Sends ausloesen. */
    void tick();

    //==========================================================================
    // Distanz -> Blink-Halbperiode in Ticks (60-Hz-Annahme): weit = langsam,
    // nah = schnell ("wie weit man drehen muss", User 15.07.2026).
    static constexpr int   kBlinkSlowTicks   = 20;   // d >= kBlinkFarDistance
    static constexpr int   kBlinkFastTicks   = 4;    // d -> Epsilon
    static constexpr float kBlinkFarDistance = 0.5f;

    static constexpr int kLedOn = 127;

    // M7b: Dauer des 16tel-Flackerns nach einem Ebenen-Wechsel (~0,6 s bei
    // 60 Hz) und die Beat-Unterteilungen der tempo-synchronen Blinks.
    static constexpr int    kLayerChangeTicks   = 36;
    static constexpr double kEighthBeats        = 0.5;    // aktives Pad blinkt im 8tel
    static constexpr double kSixteenthBeats     = 0.25;   // Ebenen-Wechsel im 16tel

    // Feedback-meanings (Konvention "Conduit Controller Profile v1", M6/M7)
    static constexpr const char* kMeaningStatusRed   = "status_red";
    static constexpr const char* kMeaningStatusAmber = "status_amber";
    static constexpr const char* kMeaningStatusGreen = "status_green";
    static constexpr const char* kMeaningLedPickup   = "led_pickup";
    static constexpr const char* kMeaningLed         = "led";           // Pad-Basis (rot)
    static constexpr const char* kMeaningLedLayerB   = "led_layer_b";   // orange
    static constexpr const char* kMeaningLedLayerC   = "led_layer_c";   // gruen

private:
    /** Kanal-lose LED-Adresse (Feedback laeuft immer auf dem Geraete-Kanal). */
    struct LedAddress
    {
        bool isNote = true;
        int  number = -1;

        auto operator<=> (const LedAddress&) const = default;
    };

    /** Soll-Zustand einer verwalteten LED in diesem Tick. */
    struct DesiredLed
    {
        bool  blink      = false;
        int   solidValue = 0;      // nur solid: Zielwert (0 = bewusst aus)
        float distance01 = 0.0f;   // nur blink: steuert die Rate (distanz-kodiert)
    };

    struct BlinkPhase
    {
        int  counter = 0;
        bool on      = true;   // Eintritt = sofort sichtbarer erster Puls
    };

    [[nodiscard]] std::map<LedAddress, DesiredLed> computeDesired() const;

    [[nodiscard]] const grid::MidiInBindings::PickupState* waitingFor (
        AddressKind kind, int number) const noexcept;

    [[nodiscard]] static int halfPeriodTicksFor (float distance01) noexcept;

    void sendValue (const LedAddress& address, int value7bit) const;

    ControllerProfile profile;
    bool hasProfile = false;

    // Warte-Zustaende pro Eingangs-Adresse (nur wartende Eintraege).
    std::map<grid::InputAddress, grid::MidiInBindings::PickupState> waiting;

    // Aktuell gehaltene Controller-Noten (Kandidaten fuer Status-Pushes).
    std::set<int> heldNotes;

    // M7b: aktive Ebene je Spalte (dauerhafte Basis-Anzeige) + laufendes
    // 16tel-Wechsel-Fenster (Rest-Ticks) + Beat-Position fuer die Blinks.
    std::map<juce::String, int> columnLayer;
    std::map<juce::String, int> layerChangeWindow;
    double beatPosition = 0.0;

    /** tempo-synchroner Blink: an in der ersten Haelfte jeder Unterteilung. */
    [[nodiscard]] bool beatBlinkOn (double subdivisionBeats) const noexcept;

    // Verwaltete LEDs: zuletzt gesendeter Wert + Blink-Phasen.
    std::map<LedAddress, int> lastSent;
    std::map<LedAddress, BlinkPhase> blinkPhases;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PickupLedRouter)
};

} // namespace conduit::midirig
