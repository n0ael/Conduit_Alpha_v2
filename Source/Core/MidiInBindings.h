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
/** Zuordnung externer CC-Eingaenge zu Conduit-Controls (Block G): pro
    Control-Wert (MacroControlKey) genau EIN Eingangs-CC (Kanal + Nummer).
    Eingehende Werte laufen durch Soft-Takeover (kein Parametersprung) und
    eine Eingangs-Glaettung (127-Stufen-CC → kontinuierliche Fahrt, One-Pole
    pro Tick -- die Anzeige folgt weich, nachgelagerte Macro-Ziele/OSC
    bekommen dieselben weichen Werte).

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
        int cc      = 1;   // 0..127
        MacroControlKey key;

        SoftTakeover takeover;
        float target01   = -1.0f;   // letzter eingegangener Zielwert (-1 = keiner)
        float smoothed01 = -1.0f;   // Glaettungszustand
        bool  pending    = false;   // Glaettung laeuft noch
    };

    /** Bindet key an (channel, cc) -- ersetzt eine bestehende Bindung
        desselben Keys UND eine bestehende Bindung desselben (channel, cc). */
    void bind (const MacroControlKey& key, int channel, int cc);
    void unbind (const MacroControlKey& key);

    [[nodiscard]] const Binding* bindingFor (const MacroControlKey& key) const noexcept;
    [[nodiscard]] int count() const noexcept { return (int) bindings.size(); }

    /** Eingehender CC [Message Thread, vom MidiControlInput gepumpt]. */
    void handleIncomingCc (int channel, int cc, int value7bit);

    /** Glaettungs-Tick (~60 Hz): schreibt anstehende Werte weich fort und
        wendet sie ueber Soft-Takeover an. currentValueFor liefert den
        Ist-Wert des Controls, applyValue setzt ihn (Besitzer: GridPage). */
    void tick (const std::function<float (const MacroControlKey&)>& currentValueFor,
               const std::function<void (const MacroControlKey&, float)>& applyValue);

    /** Lokale Beruehrung eines Controls: Takeover der Bindung loesen
        (der externe Fader muss neu aufnehmen). */
    void notifyLocalTouch (const MacroControlKey& key) noexcept;

    //==========================================================================
    // MIDI-Learn (Block G, User-Wunsch): armLearn scharfschalten, der
    // NAECHSTE eingehende CC bindet (channel, cc) an den Key und meldet
    // onLearnCompleted (UI aktualisiert Felder). Der Lern-CC selbst wird
    // danach normal verarbeitet (Takeover nimmt natuerlich auf).

    void armLearn (const MacroControlKey& key) noexcept;
    void cancelLearn() noexcept { learnArmed = false; }
    [[nodiscard]] bool isLearnArmed() const noexcept { return learnArmed; }

    std::function<void (const MacroControlKey&, int channel, int cc)> onLearnCompleted;

    /** LED-/Motorfader-Echo (channel, cc, value01) -- nur Schnittstelle. */
    std::function<void (int, int, float)> onFeedbackEcho;

    // Glaettung: Anteil der Restdistanz pro Tick; darunter wird eingerastet.
    static constexpr float kSmoothingPerTick = 0.35f;
    static constexpr float kSmoothingSnap    = 0.004f;

private:
    std::vector<Binding> bindings;

    bool learnArmed = false;
    MacroControlKey learnKey;
};

} // namespace conduit::grid
