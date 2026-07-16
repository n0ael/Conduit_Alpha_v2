#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include <juce_core/juce_core.h>

#include "MacroBindings.h"
#include "RelativeEncoding.h"

namespace conduit::grid
{

/** Pickup-Toleranz (M6): dieselbe Konstante gated den Takeover UND definiert
    "wartet auf Pickup" -- LED-Aussage und Gate duerfen nie divergieren. */
inline constexpr float kPickupEpsilon = 0.03f;

//==============================================================================
/** M8: Pitch-Bend-Eingaenge (Motorfader/Ribbon, AlphaTrack) leben im
    CC-Nummernraum OBERHALB von 127: Bindungs-Nummer = 128 + PB-Kanal
    (129..144). Damit bleibt eine Bindung ein (channel, nummer, isNote)-
    Tripel (Persistenz unveraendert, alte Sessions kennen keine Nummer
    > 127) und zwei PB-Adressen verschiedener Kanaele (AlphaTrack: Fader
    ch1, Strip ch10) kollidieren nie im kanal-agnostischen Profil-Matching. */
inline constexpr int kPitchBendBindingBase = 128;

[[nodiscard]] inline int pitchBendBindingNumber (int channel) noexcept
{
    return kPitchBendBindingBase + juce::jlimit (1, 16, channel);
}

/** M8: EINGANGS-Modus einer Adresse (profil-getrieben, Besitzer GridPage --
    MidiInBindings bleibt profil-agnostisch, die Tabelle wird via
    setAddressMode gefuettert):
    - absolute: Default -- Soft-Takeover/Pickup wie gehabt.
    - direct:   Motorfader (position-Feedback) -- Werte greifen sofort, die
                Adresse wartet NIE (der Motor steht per Definition richtig).
    - scrub:    absoluter Positions-Strom relativ angewendet (Ribbon,
                User-Entscheidung 16.07.2026): Delta zur letzten Position,
                nach einer Pause > kScrubGapTicks ankert das naechste Event
                neu (kein Sprung beim Neuaufsetzen).
    - relativeTicks: Ticks eines Endlos-Encoders; WIE sie kodiert sind, sagt
                die `RelativeEncoding` der Adresse (geraeteabhaengig, aus dem
                Profil -- siehe RelativeEncoding.h). */
enum class AddressMode { absolute, direct, scrub, relativeTicks };

//==============================================================================
/** Soft-Takeover (Block G, Masterplan: "OHNE Parametersprung"): ein externer
    CC-Wert greift erst, wenn er den aktuellen Control-Wert KREUZT oder ihm
    nahe genug kommt (Pickup). Lokales Anfassen des Controls loest den
    Takeover wieder (disengage) -- der externe Fader muss neu aufnehmen.
    Headless, testbar. */
struct SoftTakeover
{
    /** true, wenn incoming ab jetzt angewendet werden darf. current/incoming
        in [0,1]; merkt sich den letzten Eingangswert fuer die
        Kreuzungs-Erkennung. */
    bool shouldApply (float current, float incoming, float epsilon = kPickupEpsilon) noexcept
    {
        if (! engaged)
        {
            const auto near = std::abs (incoming - current) <= epsilon;
            const auto crossed = hasLastIncoming
                                     && (lastIncoming - current) * (incoming - current) <= 0.0f;
            if (near || crossed)
                engaged = true;
        }

        lastIncoming = incoming;
        hasLastIncoming = true;
        return engaged;
    }

    void disengage() noexcept
    {
        engaged = false;
        hasLastIncoming = false;
    }

    bool  engaged = false;
    bool  hasLastIncoming = false;
    float lastIncoming = 0.0f;
};

//==============================================================================
/** Ein Shift-Modifier einer Bindung (MIDI-Rig M5): eine gehaltene Note
    (Pad) auf dem Controller. Kanonische Ordnung fuer Set-Vergleiche. */
struct ModifierNote
{
    int channel = 1;   // 1..16
    int note    = 0;   // 0..127

    auto operator<=> (const ModifierNote&) const = default;
};

/** Sortiertes, duplikatfreies Modifier-Set (kanonisch — bind() normalisiert). */
using ModifierSet = std::vector<ModifierNote>;

//==============================================================================
/** Physische Eingangs-Adresse eines Controller-Controls (M6): ueber alle
    Shift-Ebenen hinweg gibt es pro Adresse genau EINE Hardware-Position. */
struct InputAddress
{
    int channel = 1;       // 1..16
    int number  = 0;       // CC- bzw. Notennummer (0..127)
    bool isNote = false;

    auto operator<=> (const InputAddress&) const = default;
};

//==============================================================================
/** Zuordnung externer CC-Eingaenge zu Conduit-Controls (Block G): pro
    Control-Wert (MacroControlKey) genau EIN Eingangs-CC (Kanal + Nummer).
    Eingehende Werte laufen durch Soft-Takeover (kein Parametersprung) und
    eine Eingangs-Glaettung (127-Stufen-CC → kontinuierliche Fahrt, One-Pole
    pro Tick -- die Anzeige folgt weich, nachgelagerte Macro-Ziele/OSC
    bekommen dieselben weichen Werte).

    Shift-Ebenen (M5, User-Entscheidung 14.07.2026): eine Bindung darf
    zusaetzlich ein Modifier-Set (gehaltene Pad-Noten, auch Akkorde) tragen —
    dieselbe Adresse existiert dann mehrfach mit unterschiedlichen Ebenen
    (fader1 vs. pad1+fader1). Pro Eingangs-Event feuert genau EINE Bindung:
    die mit dem groessten Modifier-Set, das vollstaendig gehalten ist
    (exakteste Ebene gewinnt; bei Gleichstand die zuletzt gebundene).
    Modifier-Pads behalten ihre Eigenfunktion (Default); optional
    suppressWhileShift: die Eigenfunktion feuert erst beim Loslassen und
    NUR, wenn das Pad in diesem Halten nicht als Shift gedient hat.

    LED-Feedback/Motorfader: onFeedbackEcho ist die vorgesehene
    SCHNITTSTELLE (Masterplan Block G: nur vorsehen) -- sie feuert, wenn ein
    Wert tatsaechlich angewendet wurde; eine Hardware-Implementierung dockt
    spaeter dort an.

    Laufzeit-only (Persistenz Block K). Message Thread. */
class MidiInBindings
{
public:
    struct Binding
    {
        int channel = 1;   // 1..16
        int cc      = 1;   // CC-Nummer bzw. Notennummer (0..127)
        bool isNote = false;   // M4: Pads senden Noten statt CCs
        ModifierSet modifiers;          // M5: Shift-Ebene (leer = Basis-Ebene)
        bool suppressWhileShift = false;   // M5: Eigenfunktion stumm, wenn als Shift gedient

        // M7 Channelstrip-Ebene: `column` = Profil-group der Spalte (leer =
        // nicht geebent -> immer aktiv), `layer` = Bank-Index dieser Spalte
        // (nur relevant bei nicht-leerer column). bestMatch filtert eine
        // geebente Bindung aus, wenn ihre Ebene nicht die aktive der Spalte ist.
        juce::String column;
        int layer = -1;

        MacroControlKey key;

        SoftTakeover takeover;
        float target01   = -1.0f;   // letzter eingegangener Zielwert (-1 = keiner)
        float smoothed01 = -1.0f;   // Glaettungszustand
        bool  pending    = false;   // Glaettung laeuft noch
        bool  noteHeld   = false;   // M5: Note-On hat DIESE Ebene gewaehlt (Off-Routing)
        float deferredPress01 = -1.0f;   // M5: aufgeschobener Press (suppressWhileShift)
        bool  pulseRelease    = false;   // M5: nach dem Puls-Press auf 0 zurueck
        float pendingDelta01  = 0.0f;    // M8: akkumuliertes Delta (scrub/relativeTicks)
    };

    /** Sentinel: "Spalte/Ebene aus columnResolver + aktueller aktiver Ebene
        aufloesen" (Live-/Learn-Pfad). Explizite Werte (inkl. leer/-1 =
        nicht geebent) nimmt der Persistenz-Load. */
    static const juce::String kAutoColumn;

    /** Bindet key an (channel, cc/note) mit optionalem Modifier-Set --
        ersetzt eine bestehende Bindung desselben Keys UND eine bestehende
        Bindung derselben Adresse (channel, nummer, isNote) MIT identischem
        Modifier-Set UND identischer Channelstrip-Ebene (M5/M7:
        unterschiedliche Ebenen derselben Adresse koexistieren).
        `column == kAutoColumn` (Default) loest Spalte/Ebene ueber den
        columnResolver + die aktive Ebene auf (Live/Learn: "aktive Ebene =
        Lernziel"); explizite Werte kommen aus der Persistenz. */
    void bind (const MacroControlKey& key, int channel, int cc, bool isNote = false,
               ModifierSet modifiers = {}, bool suppressWhileShift = false,
               const juce::String& column = kAutoColumn, int layer = -1);
    void unbind (const MacroControlKey& key);

    //==========================================================================
    // M7 Channelstrip-Ebenen: die aktive Ebene je Spalte + der Resolver, der
    // eine Eingangs-Adresse ihrer Profil-Spalte zuordnet (Besitzer: GridPage,
    // profil-getrieben). MidiInBindings bleibt profil-agnostisch -- `column`
    // ist ein opaker String-Schluessel.

    /** Aktive Ebene einer Spalte setzen (Encoder-Auswahl / Persistenz-Load).
        Wirkt sofort auf das naechste bestMatch. */
    void setActiveLayer (const juce::String& column, int layer);
    [[nodiscard]] int activeLayer (const juce::String& column) const noexcept;

    /** Ordnet eine Eingangs-Adresse ihrer Spalte zu (leer = nicht geebent).
        Wird beim Live-/Learn-Bind gerufen, um Spalte/Ebene zu taggen. */
    std::function<juce::String (int channel, int number, bool isNote)> columnResolver;

    /** M5b (Mappings-Liste): Suppress-Flag einer bestehenden Bindung
        umschalten -- kein Effekt bei unbekanntem Key. */
    void setSuppressWhileShift (const MacroControlKey& key, bool shouldSuppress);

    /** M5b: feuert nach jeder Struktur-Änderung (bind/unbind/Suppress) --
        die Mappings-Liste im Map-Tab baut sich damit neu auf. */
    std::function<void()> onBindingsChanged;

    [[nodiscard]] const Binding* bindingFor (const MacroControlKey& key) const noexcept;
    [[nodiscard]] int count() const noexcept { return (int) bindings.size(); }

    /** Alle Bindungen (Block-K-Persistenz, nur lesen). */
    [[nodiscard]] const std::vector<Binding>& all() const noexcept { return bindings; }

    /** Eingehender CC [Message Thread, vom MidiPortHub-Drain gepumpt]. */
    void handleIncomingCc (int channel, int cc, int value7bit);

    /** M8: Eingehender Pitch Bend (Motorfader/Ribbon) [Message Thread] --
        laeuft als Bindungs-Nummer pitchBendBindingNumber(channel) durch
        denselben Pfad wie CCs (Learn inklusive), value14 = 0..16383. */
    void handleIncomingPitchBend (int channel, int value14);

    //==========================================================================
    // M8: Adress-Modi (profil-getrieben, Besitzer GridPage). Kanal-agnostisch
    // gekeyt auf (nummer, isNote) -- PB-Adressen tragen ihren Kanal in der
    // Nummer (s. pitchBendBindingNumber). relativeSteps: Ticks fuer den
    // vollen Regelweg (nur relativeTicks; 0 = kDefaultRelativeSteps).
    // relEncoding: nur relativeTicks -- Kodierung des Encoders (RelativeEncoding.h).

    void setAddressMode (int number, bool isNote, AddressMode mode, int relativeSteps = 0,
                         midirig::RelativeEncoding relEncoding = midirig::RelativeEncoding::twosComplement);
    void clearAddressModes();
    [[nodiscard]] AddressMode addressModeFor (int number, bool isNote) const noexcept;

    /** M8 (PositionFeedbackRouter): aktive Bindung einer Adresse -- KANAL-
        agnostisch (M4b: der Kanal ist Geraete-Eigenschaft), sonst identische
        Auswahl wie der Eingangs-Pfad: groesstes vollstaendig gehaltenes
        Modifier-Set, geebente Bindungen nur auf der aktiven Ebene ihrer
        Spalte. nullptr = keine aktive Bindung (Motor bleibt stehen). */
    [[nodiscard]] const Binding* activeBindingForAddress (int number, bool isNote) const noexcept;

    static constexpr int kDefaultRelativeSteps = 127;   // Ticks fuer den vollen Weg
    static constexpr std::uint64_t kScrubGapTicks = 15; // ~250 ms bei 60 Hz

    /** Eingehende Note [Message Thread] (M4: Controller-Pads) --
        Note-On setzt Velocity/127 als Zielwert (Momentary + Velocity,
        User-Entscheidung 14.07.2026), Note-Off (bzw. Velocity 0) setzt 0.
        Ein scharfes Learn macht die Note zum Kandidaten; gebunden wird
        beim Loslassen aller Noten (M5 Chord-Learn, s. o.). */
    void handleIncomingNote (int channel, int note, int velocity7bit, bool isOn);

    /** Glaettungs-Tick (~60 Hz): schreibt anstehende Werte weich fort und
        wendet sie ueber Soft-Takeover an. currentValueFor liefert den
        Ist-Wert des Controls, applyValue setzt ihn (Besitzer: GridPage). */
    void tick (const std::function<float (const MacroControlKey&)>& currentValueFor,
               const std::function<void (const MacroControlKey&, float)>& applyValue);

    /** Lokale Beruehrung eines Controls: Takeover der Bindung loesen
        (der externe Fader muss neu aufnehmen). */
    void notifyLocalTouch (const MacroControlKey& key) noexcept;

    //==========================================================================
    // MIDI-Learn (Block G + M5 Chord-Learn): armLearn scharfschalten.
    // Ein eingehender CC bindet SOFORT — mit den gerade gehaltenen Noten
    // als Modifier-Set (Pad halten + Fader bewegen = Shift-Ebene lernen).
    // Werden nur Noten gedrueckt und alle wieder losgelassen, bindet die
    // ZULETZT gedrueckte Note mit den uebrigen gehaltenen als Modifier
    // (Pad-Akkord-Bindung). onLearnCompleted meldet Adresse + Modifier.

    void armLearn (const MacroControlKey& key) noexcept;
    void cancelLearn() noexcept;
    [[nodiscard]] bool isLearnArmed() const noexcept { return learnArmed; }

    std::function<void (const MacroControlKey&, int channel, int cc, bool isNote,
                        const ModifierSet& modifiers)> onLearnCompleted;

    /** LED-/Motorfader-Echo (channel, nummer, isNote, value01) -- feuert,
        wenn ein Wert tatsaechlich angewendet wurde (M4: GridPage sendet
        darueber Feedback an das Controller-Rollen-Geraet). */
    std::function<void (int, int, bool, float)> onFeedbackEcho;

    //==========================================================================
    // Pickup-Status (M6): pro Adresse meldet tick(), ob die aktive Ebene
    // (Best-Match bei den aktuell gehaltenen Modifiern) auf Pickup wartet --
    // d. h. Takeover nicht engaged UND die physische Position BEKANNT und
    // weiter als kPickupEpsilon vom Software-Wert entfernt. Unbekannte
    // Position (App-Start, nie bewegt) wartet NIE: Blink = belegter Konflikt.

    struct PickupState
    {
        bool  waiting    = false;
        float distance01 = 0.0f;      // |physisch - Software| der aktiven Ebene
        ModifierSet modifiers;        // Modifier-Set der aktiven Ebene (leer = Basis)
        bool  activeRecently = false; // Eingangs-Event innerhalb kActivityHoldTicks

        // M6.1 (Shift-Pad Richtungsanzeige, User 15.07.2026): das Vorzeichen der
        // Distanz signalisiert die Drehrichtung, `aligned` das Abholen. Beide
        // NUR fuer Shift-Ebenen gefuellt (Modifier nicht leer) -- der Router
        // faerbt damit die gehaltenen Modifier-Pads solide (rot=verringern,
        // orange=erhoehen, gruen=gefunden), waehrend die Spalten-Status-LED
        // weiterhin distanz-kodiert blinkt.
        bool  physicalAbove = false;  // physisch > Software -> Wert verringern (rot)
        bool  aligned       = false;  // abgeholt/engaged innerhalb Epsilon (gruen)
    };

    /** Feuert am Tick-Ende bei jedem Zustandswechsel (waiting-/aligned-Flanke)
        sowie waehrend des Wartens bei Distanz-/Richtungs-/Aktivitaets-Aenderung
        (Dedupe). waiting=false UND aligned=false ⇒ der Konsument restauriert
        seine LEDs. */
    std::function<void (const InputAddress&, const PickupState&)> onPickupStateChanged;

    /** M6 Takeover-Modus des Controller-Geraets: false = "Sprung" (Werte
        greifen sofort, nie waiting). Idempotent -- laeuft bei jedem
        Registry-Broadcast. Rueckschalten auf Pickup disengaged alle
        Bindungen (Engagements nach Spruengen sind bedeutungslos). */
    void setPickupEnabled (bool enabled);
    [[nodiscard]] bool isPickupEnabled() const noexcept { return pickupEnabled; }

    // Glaettung: Anteil der Restdistanz pro Tick; darunter wird eingerastet.
    static constexpr float kSmoothingPerTick = 0.35f;
    static constexpr float kSmoothingSnap    = 0.004f;

    // Pickup-Status: Aktivitaets-Fenster + Melde-Schwelle der Distanz
    // (Tick-Basis = 60-Hz-Hub-Drain).
    static constexpr std::uint64_t kActivityHoldTicks   = 30;   // ~0,5 s
    static constexpr float         kDistanceReportDelta = 0.005f;

private:
    /** Gemeinsamer Eingangs-Pfad von CC und Note: beste Ebene waehlen
        (Modifier-Spezifitaet), Zielwert setzen, Glaettung anstossen.
        noteOn steuert das Off-Routing (M5: Off geht an die per On
        gewaehlte Ebene, nicht an die aktuell passendste). */
    void applyIncoming (int channel, int number, bool isNote, bool noteOn, float value01);

    /** Beste Bindung fuer eine Adresse: groesstes Modifier-Set, das
        vollstaendig in heldNotes liegt (Gleichstand: zuletzt gebundene). */
    [[nodiscard]] Binding* bestMatch (int channel, int number, bool isNote) noexcept;

    void markModifiersUsed (const ModifierSet& modifiers);
    void setBindingTarget (Binding& binding, float value01);

    /** M8 scrub: Positions-Strom -> Delta auf die aktive Ebene (Anker nach
        Pause, s. AddressMode). value01 = rohe absolute Position. */
    void applyScrub (int channel, int number, float value01);

    /** M8 relativeTicks: signed 7-bit-Delta auf die aktive Ebene. */
    void applyRelativeTicks (int channel, int number, int value7bit);

    /** M6: Warte-Zustaende neu bewerten und Transitionen melden -- laeuft am
        ENDE von tick(), damit Engagements/Echos desselben Ticks sichtbar sind
        (sonst flackert der Uebergang Pickup -> Anzeige). */
    void updatePickupStates (const std::function<float (const MacroControlKey&)>& currentValueFor);

    std::vector<Binding> bindings;

    // M6: geteilte Physik pro Adresse (transient, roher Eingangswert) +
    // zuletzt gemeldete Warte-Zustaende (nur wartende Adressen) + Aktivitaet.
    bool pickupEnabled = true;
    std::uint64_t tickCounter = 0;
    std::map<InputAddress, float> physicalPositions;
    std::map<InputAddress, std::uint64_t> lastActivityTick;
    std::map<InputAddress, PickupState> pickupStates;

    ModifierSet heldNotes;     // aktuell gehaltene Noten (Shift-Kandidaten)
    ModifierSet usedAsShift;   // Noten, die in diesem Halten als Shift dienten

    std::map<juce::String, int> activeLayerByColumn;   // M7: aktive Bank je Spalte

    // M8: Adress-Modi (Key {nummer, isNote}, kanal-agnostisch) + Scrub-Anker
    // pro Adresse (letzte rohe Position + Tick des letzten Events).
    struct AddressModeEntry
    {
        AddressMode mode = AddressMode::absolute;
        int steps = 0;
        midirig::RelativeEncoding relEncoding = midirig::RelativeEncoding::twosComplement;
    };
    struct ScrubAnchor      { float raw = 0.0f; std::uint64_t tick = 0; };
    std::map<std::pair<int, bool>, AddressModeEntry> addressModes;
    std::map<InputAddress, ScrubAnchor> scrubAnchors;

    bool learnArmed = false;
    MacroControlKey learnKey;
    bool hasLearnCandidate = false;   // M5 Chord-Learn: zuletzt gedrueckte Note
    ModifierNote learnCandidate;
    ModifierSet  learnCandidateModifiers;
};

} // namespace conduit::grid
