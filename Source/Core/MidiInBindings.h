#pragma once

#include <functional>
#include <vector>

#include <juce_core/juce_core.h>

#include "MacroBindings.h"

namespace conduit::grid
{

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
    bool shouldApply (float current, float incoming, float epsilon = 0.03f) noexcept
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
        MacroControlKey key;

        SoftTakeover takeover;
        float target01   = -1.0f;   // letzter eingegangener Zielwert (-1 = keiner)
        float smoothed01 = -1.0f;   // Glaettungszustand
        bool  pending    = false;   // Glaettung laeuft noch
        bool  noteHeld   = false;   // M5: Note-On hat DIESE Ebene gewaehlt (Off-Routing)
        float deferredPress01 = -1.0f;   // M5: aufgeschobener Press (suppressWhileShift)
        bool  pulseRelease    = false;   // M5: nach dem Puls-Press auf 0 zurueck
    };

    /** Bindet key an (channel, cc/note) mit optionalem Modifier-Set --
        ersetzt eine bestehende Bindung desselben Keys UND eine bestehende
        Bindung derselben Adresse (channel, nummer, isNote) MIT identischem
        Modifier-Set (M5: unterschiedliche Ebenen koexistieren). */
    void bind (const MacroControlKey& key, int channel, int cc, bool isNote = false,
               ModifierSet modifiers = {}, bool suppressWhileShift = false);
    void unbind (const MacroControlKey& key);

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

    // Glaettung: Anteil der Restdistanz pro Tick; darunter wird eingerastet.
    static constexpr float kSmoothingPerTick = 0.35f;
    static constexpr float kSmoothingSnap    = 0.004f;

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

    std::vector<Binding> bindings;

    ModifierSet heldNotes;     // aktuell gehaltene Noten (Shift-Kandidaten)
    ModifierSet usedAsShift;   // Noten, die in diesem Halten als Shift dienten

    bool learnArmed = false;
    MacroControlKey learnKey;
    bool hasLearnCandidate = false;   // M5 Chord-Learn: zuletzt gedrueckte Note
    ModifierNote learnCandidate;
    ModifierSet  learnCandidateModifiers;
};

} // namespace conduit::grid
