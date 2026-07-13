#pragma once

#include <map>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include "Interfaces/IMidiOutputTarget.h"
#include "ResponseCurve.h"

namespace conduit::grid
{

//==============================================================================
/** Ziel eines Macro-Bindings (Block E, Masterplan): gemeinsames Wert-Modell
    -- der Aufrufer liefert 0..1 (bereits durch die Binding-Kurve geformt),
    die Implementierung uebersetzt in ihr Protokoll. Message Thread. */
class MacroTarget
{
public:
    virtual ~MacroTarget() = default;

    virtual void sendValue (float value01) = 0;

    /** Kurzbeschreibung fuer die Ziel-Zeile im Macro-Panel
        (z. B. "CC 74 / Kanal 1" oder "Wavetable: Osc 1 Pos"). */
    [[nodiscard]] virtual juce::String describe() const = 0;

    /** Persistenz (Block K): serialisierbarer Zustand des Ziels — ein
        ungueltiger Tree (Default) heisst "nicht persistierbar". Der
        GridSessionStore speichert das Tree OPAK; den Rueckweg baut eine
        Factory des Besitzers (GridPage kennt die konkreten Typen). */
    [[nodiscard]] virtual juce::ValueTree toState() const { return {}; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacroTarget)

protected:
    MacroTarget() = default;
};

//==============================================================================
/** Re-Resolve-Merkmale eines Ableton-Parameter-Ziels (Block K): dvid ist
    eine LAUFZEIT-Stable-ID (Rule touchlive) — persistiert werden NUR
    stabile Merkmale, aus denen das Ziel nach Live-Neustart neu aufgeloest
    wird: Track-NAME, Device-NAME + Ordinal (n-tes gleichnamiges Device in
    der Chain) und Parameter-NAME. POD, TouchLive-frei (der Store und die
    Tests brauchen keinen TouchLiveClient). */
struct LiveParamSpec
{
    juce::String trackName;
    juce::String deviceName;
    int          deviceOrdinal = 0;   // 0 = erstes gleichnamiges Device
    juce::String paramName;
    juce::String displayName;         // Anzeige der Ziel-Zeile ("Device: Param")
};

//==============================================================================
/** MIDI-CC-Ziel: Kanal + CC-Nummer auf den Grid-MIDI-Ausgang. Dedupliziert
    auf 7-bit-Aufloesung (127-Stufen-Ziel bekommt nur echte Aenderungen --
    die Glaettung kontinuierlicher Fahrten ist Sache der Zielschicht,
    Masterplan Block E/G). */
class MidiCcTarget final : public MacroTarget
{
public:
    MidiCcTarget (IMidiOutputTarget& outputToUse, int channelToUse, int ccNumberToUse);

    void sendValue (float value01) override;
    [[nodiscard]] juce::String describe() const override;
    [[nodiscard]] juce::ValueTree toState() const override;   // Block K

    [[nodiscard]] int channel() const noexcept { return midiChannel; }
    [[nodiscard]] int ccNumber() const noexcept { return cc; }

    static inline const juce::Identifier kStateType { "MidiTarget" };

private:
    IMidiOutputTarget& output;
    int midiChannel;   // 1..16
    int cc;            // 0..127
    int lastSent = -1; // 7-bit-Dedupe

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiCcTarget)
};

//==============================================================================
/** MIDI-NRPN-Ziel (MIDI-Rig M2, ADR 006 E4): 14-bit-Parameter über die
    Sequenz CC99 (Adress-MSB) → CC98 (Adress-LSB) → CC6 (Daten-MSB) →
    CC38 (Daten-LSB). value01 wird auf [minValue, maxValue] des Geräte-
    Profils gemappt (Analog Heat: 0..16383); Dedupe auf dem gemappten
    Wert. Der Anzeigename kommt aus dem Profil (Picker) — persistiert
    werden nur die stabilen Nummern. */
class MidiNrpnTarget final : public MacroTarget
{
public:
    MidiNrpnTarget (IMidiOutputTarget& outputToUse, int channelToUse, int nrpnNumberToUse,
                    int minValueToUse, int maxValueToUse,
                    const juce::String& displayNameToUse = {});

    void sendValue (float value01) override;
    [[nodiscard]] juce::String describe() const override;
    [[nodiscard]] juce::ValueTree toState() const override;

    [[nodiscard]] int channel() const noexcept { return midiChannel; }
    [[nodiscard]] int nrpnNumber() const noexcept { return nrpn; }

    /** MIDI-Rig M3: fuers Kanal-only-Rebuild eines bestehenden (z. B. aus
        einer Session geladenen) Ziels -- der Picker selbst braucht diese
        Getter nicht, nur MacroPanel::TargetRow ohne frische Picker-Auswahl. */
    [[nodiscard]] int rangeMin() const noexcept { return minValue; }
    [[nodiscard]] int rangeMax() const noexcept { return maxValue; }
    [[nodiscard]] const juce::String& name() const noexcept { return displayName; }

    static inline const juce::Identifier kStateType { "NrpnTarget" };

private:
    IMidiOutputTarget& output;
    int midiChannel;    // 1..16
    int nrpn;           // 0..16383 (msb*128+lsb)
    int minValue;
    int maxValue;
    juce::String displayName;
    int lastSent = -1;  // Dedupe auf dem gemappten Wert

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiNrpnTarget)
};

//==============================================================================
/** Program-Change-Ziel (MIDI-Rig M2, ADR 006 E5): value01 → Programm
    0..127 (Dedupe), optional mit Bank-Select-Vorstufe CC0/CC32
    (bankMsb/bankLsb, -1 = keine Bank). */
class MidiProgramChangeTarget final : public MacroTarget
{
public:
    MidiProgramChangeTarget (IMidiOutputTarget& outputToUse, int channelToUse,
                             int bankMsbToUse = -1, int bankLsbToUse = -1);

    void sendValue (float value01) override;
    [[nodiscard]] juce::String describe() const override;
    [[nodiscard]] juce::ValueTree toState() const override;

    /** MIDI-Rig M3: fuers Kanal-only-Rebuild eines geladenen Ziels
        (MacroPanel::TargetRow::rebuildFromBinding). */
    [[nodiscard]] int channel() const noexcept { return midiChannel; }

    static inline const juce::Identifier kStateType { "PcTarget" };

private:
    IMidiOutputTarget& output;
    int midiChannel;   // 1..16
    int bankMsb;       // -1 = keine Bank-Vorstufe
    int bankLsb;
    int lastSent = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiProgramChangeTarget)
};

//==============================================================================
/** Adressiert die Macro-Bindings eines Control-WERTS: Layer (System-Controls
    des XY+Fader-Modus vs. DIY-CC-Baukasten -- deren Control-Ids kollidieren,
    beide Modelle zaehlen ab 1), Control-Id und Achse (XY-Pads haben zwei
    unabhaengige Macro-Quellen: 0 = X bzw. der Primaerwert von Fader/Push/
    Toggle, 1 = Y). */
struct MacroControlKey
{
    enum Layer { system = 0, diy = 1 };

    int layer     = system;
    int controlId = 0;
    int axis      = 0;

    auto operator<=> (const MacroControlKey&) const = default;
};

//==============================================================================
/** Ein Ziel-Slot eines Controls: Kurve (2 Punkte + Kruemmung, OHNE Offset --
    Masterplan Block E) vor dem MacroTarget; target == nullptr = Slot ist
    angelegt, aber noch keinem Ziel zugewiesen. lastInput/lastOutput sind die
    Anzeige-Werte der Compact-View (Punkt auf der Linie). */
struct MacroBinding
{
    MacroBinding() = default;   // deleted Copy-Ctor (Makro) unterdrueckt sonst den Default-Ctor

    std::unique_ptr<MacroTarget> target;
    ResponseCurve curve;
    float lastInput01  = 0.0f;
    float lastOutput01 = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacroBinding)
};

//==============================================================================
/** Store aller Macro-Bindings, keyed nach MacroControlKey -- bewusst
    GETRENNT vom CcControlModel (das bleibt UI-/MIDI-freies POD; die Ids
    recyceln bei clear(), daher muss der Besitzer clearControl() mitziehen).
    Laufzeit-only, Persistenz kommt gebuendelt in Block K. Message Thread. */
class MacroBindings
{
public:
    static constexpr int kMaxTargetsPerControl = 16;

    MacroBindings() = default;

    /** Neuen (leeren) Ziel-Slot anlegen -- nullptr, wenn kMaxTargetsPerControl
        erreicht ist. */
    MacroBinding* add (const MacroControlKey& key);

    void remove (const MacroControlKey& key, int index);

    [[nodiscard]] int count (const MacroControlKey& key) const noexcept;
    [[nodiscard]] MacroBinding* get (const MacroControlKey& key, int index) noexcept;

    /** Alle Achsen eines Controls entfernen (Control geloescht bzw. Modell
        geleert -- Id-Recycling!). */
    void clearControl (int layer, int controlId);

    /** Alle Keys mit mindestens einem Slot (Block-K-Persistenz, Iteration
        ueber count/get) — map-sortiert, deterministisch. */
    [[nodiscard]] std::vector<MacroControlKey> allKeys() const;

    /** Wert-Fluss (Block E): value01 → pro Slot Kurve anwenden (geklemmt auf
        [0,1] -- Macro-Ziele kennen keine Achsen-Kapazitaet daruueber) →
        target->sendValue. Slots ohne Ziel aktualisieren nur die Anzeige. */
    void applyValue (const MacroControlKey& key, float value01);

private:
    std::map<MacroControlKey, std::vector<std::unique_ptr<MacroBinding>>> bindings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacroBindings)
};

} // namespace conduit::grid
