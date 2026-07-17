#include "MacroBindings.h"

#include <cmath>

#include "Util/CcNames.h"

namespace conduit::grid
{

//==============================================================================
MidiCcTarget::MidiCcTarget (IMidiOutputTarget& outputToUse, int channelToUse, int ccNumberToUse)
    : output (outputToUse),
      midiChannel (juce::jlimit (1, 16, channelToUse)),
      cc (juce::jlimit (0, 127, ccNumberToUse))
{
}

void MidiCcTarget::sendValue (float value01)
{
    const auto v = juce::jlimit (0, 127, (int) std::lround (value01 * 127.0f));

    if (v == lastSent)
        return;   // 7-bit-Dedupe: nur echte Aenderungen senden

    lastSent = v;
    output.send (juce::MidiMessage::controllerEvent (midiChannel, cc, v));
}

juce::String MidiCcTarget::describe() const
{
    // Block L: Funktionsname statt Rohnummer, wo bekannt ("Mod Wheel"
    // statt "CC 1"); unbekannte CCs fallen auf das bisherige Format zurück.
    return CcNames::displayName (cc) + " / Kanal " + juce::String (midiChannel);
}

juce::ValueTree MidiCcTarget::toState() const
{
    juce::ValueTree state (kStateType);
    state.setProperty ("channel", midiChannel, nullptr);
    state.setProperty ("cc", cc, nullptr);
    return state;
}

//==============================================================================
MidiNrpnTarget::MidiNrpnTarget (IMidiOutputTarget& outputToUse, int channelToUse,
                                int nrpnNumberToUse, int minValueToUse, int maxValueToUse,
                                const juce::String& displayNameToUse)
    : output (outputToUse),
      midiChannel (juce::jlimit (1, 16, channelToUse)),
      nrpn (juce::jlimit (0, 16383, nrpnNumberToUse)),
      minValue (juce::jlimit (0, 16383, minValueToUse)),
      maxValue (juce::jlimit (0, 16383, maxValueToUse)),
      displayName (displayNameToUse)
{
    if (maxValue <= minValue)   // kaputte Profil-Range reparieren
    {
        minValue = 0;
        maxValue = 16383;
    }
}

void MidiNrpnTarget::sendValue (float value01)
{
    const auto clamped = juce::jlimit (0.0f, 1.0f, value01);
    const auto mapped = minValue
        + (int) std::lround (clamped * (float) (maxValue - minValue));

    if (mapped == lastSent)
        return;   // Dedupe auf dem gemappten Wert (14-bit)

    lastSent = mapped;

    // ADR E4: Adresse zuerst (MSB/LSB), dann Daten-MSB/LSB — jedes send()
    // traegt die volle Sequenz, damit parallel gesendete CC-Ziele die
    // NRPN-Adresse des Geraets nicht verfaelschen koennen.
    output.send (juce::MidiMessage::controllerEvent (midiChannel, 99, nrpn / 128));
    output.send (juce::MidiMessage::controllerEvent (midiChannel, 98, nrpn % 128));
    output.send (juce::MidiMessage::controllerEvent (midiChannel, 6,  (mapped >> 7) & 0x7f));
    output.send (juce::MidiMessage::controllerEvent (midiChannel, 38, mapped & 0x7f));
}

juce::String MidiNrpnTarget::describe() const
{
    const auto base = displayName.isNotEmpty() ? displayName
                                               : "NRPN " + juce::String (nrpn);
    return base + " / Kanal " + juce::String (midiChannel);
}

juce::ValueTree MidiNrpnTarget::toState() const
{
    juce::ValueTree state (kStateType);
    state.setProperty ("channel", midiChannel, nullptr);
    state.setProperty ("number", nrpn, nullptr);
    state.setProperty ("min", minValue, nullptr);
    state.setProperty ("max", maxValue, nullptr);
    state.setProperty ("name", displayName, nullptr);
    return state;
}

//==============================================================================
MidiProgramChangeTarget::MidiProgramChangeTarget (IMidiOutputTarget& outputToUse,
                                                  int channelToUse,
                                                  int bankMsbToUse, int bankLsbToUse)
    : output (outputToUse),
      midiChannel (juce::jlimit (1, 16, channelToUse)),
      bankMsb (juce::jlimit (-1, 127, bankMsbToUse)),
      bankLsb (juce::jlimit (-1, 127, bankLsbToUse))
{
}

void MidiProgramChangeTarget::sendValue (float value01)
{
    const auto program = juce::jlimit (0, 127, (int) std::lround (value01 * 127.0f));

    if (program == lastSent)
        return;

    lastSent = program;

    // ADR E5: optionale Bank-Select-Vorstufe (CC0 MSB / CC32 LSB), dann PC.
    if (bankMsb >= 0)
        output.send (juce::MidiMessage::controllerEvent (midiChannel, 0, bankMsb));
    if (bankLsb >= 0)
        output.send (juce::MidiMessage::controllerEvent (midiChannel, 32, bankLsb));

    output.send (juce::MidiMessage::programChange (midiChannel, program));
}

juce::String MidiProgramChangeTarget::describe() const
{
    auto text = "Program Change / Kanal " + juce::String (midiChannel);
    if (bankMsb >= 0 || bankLsb >= 0)
        text += " (Bank " + juce::String (juce::jmax (0, bankMsb))
              + "/" + juce::String (juce::jmax (0, bankLsb)) + ")";
    return text;
}

juce::ValueTree MidiProgramChangeTarget::toState() const
{
    juce::ValueTree state (kStateType);
    state.setProperty ("channel", midiChannel, nullptr);
    state.setProperty ("bankMsb", bankMsb, nullptr);
    state.setProperty ("bankLsb", bankLsb, nullptr);
    return state;
}

//==============================================================================
MidiPresetLoadTarget::MidiPresetLoadTarget (IMidiOutputTarget& outputToUse, int channelToUse,
                                            int programToUse, int bankMsbToUse, int bankLsbToUse,
                                            juce::String presetNameToUse)
    : output (outputToUse),
      midiChannel (juce::jlimit (1, 16, channelToUse)),
      programNumber (juce::jlimit (0, 127, programToUse)),
      bankMsb (juce::jlimit (-1, 127, bankMsbToUse)),
      bankLsb (juce::jlimit (-1, 127, bankLsbToUse)),
      presetName (std::move (presetNameToUse))
{
}

void MidiPresetLoadTarget::sendValue (float value01)
{
    // Druckflanke: Press sendet, Halten/Release nicht; erneuter Press
    // sendet wieder (bewusst KEIN Dedupe ueber Flanken hinweg).
    const auto pressed = value01 >= 0.5f && lastValue01 < 0.5f;
    lastValue01 = value01;

    if (! pressed)
        return;

    if (bankMsb >= 0)
        output.send (juce::MidiMessage::controllerEvent (midiChannel, 0, bankMsb));
    if (bankLsb >= 0)
        output.send (juce::MidiMessage::controllerEvent (midiChannel, 32, bankLsb));

    output.send (juce::MidiMessage::programChange (midiChannel, programNumber));
}

juce::String MidiPresetLoadTarget::describe() const
{
    auto text = "Preset: " + (presetName.isNotEmpty() ? presetName
                                                      : juce::String (programNumber + 1));
    if (bankLsb >= 0 || bankMsb >= 0)
        text += " (Bank " + juce::String (juce::jmax (0, bankMsb) * 128
                                          + juce::jmax (0, bankLsb) + 1) + ")";
    return text + " / Kanal " + juce::String (midiChannel);
}

juce::ValueTree MidiPresetLoadTarget::toState() const
{
    juce::ValueTree state (kStateType);
    state.setProperty ("channel", midiChannel, nullptr);
    state.setProperty ("program", programNumber, nullptr);
    state.setProperty ("bankMsb", bankMsb, nullptr);
    state.setProperty ("bankLsb", bankLsb, nullptr);
    state.setProperty ("name", presetName, nullptr);
    return state;
}

//==============================================================================
MacroBinding* MacroBindings::add (const MacroControlKey& key)
{
    auto& list = bindings[key];

    if ((int) list.size() >= kMaxTargetsPerControl)
        return nullptr;

    list.push_back (std::make_unique<MacroBinding>());
    return list.back().get();
}

void MacroBindings::remove (const MacroControlKey& key, int index)
{
    const auto it = bindings.find (key);
    if (it == bindings.end())
        return;

    auto& list = it->second;
    if (index < 0 || index >= (int) list.size())
        return;

    list.erase (list.begin() + index);

    if (list.empty())
        bindings.erase (it);
}

int MacroBindings::count (const MacroControlKey& key) const noexcept
{
    const auto it = bindings.find (key);
    return it == bindings.end() ? 0 : (int) it->second.size();
}

MacroBinding* MacroBindings::get (const MacroControlKey& key, int index) noexcept
{
    const auto it = bindings.find (key);
    if (it == bindings.end())
        return nullptr;

    auto& list = it->second;
    if (index < 0 || index >= (int) list.size())
        return nullptr;

    return list[(size_t) index].get();
}

void MacroBindings::clearControl (int layer, int controlId)
{
    for (auto it = bindings.begin(); it != bindings.end();)
    {
        if (it->first.layer == layer && it->first.controlId == controlId)
            it = bindings.erase (it);
        else
            ++it;
    }
}

std::vector<MacroControlKey> MacroBindings::allKeys() const
{
    std::vector<MacroControlKey> keys;
    keys.reserve (bindings.size());

    for (const auto& [key, list] : bindings)
        if (! list.empty())
            keys.push_back (key);

    return keys;
}

void MacroBindings::applyValue (const MacroControlKey& key, float value01)
{
    const auto it = bindings.find (key);
    if (it == bindings.end())
        return;

    for (auto& binding : it->second)
    {
        binding->lastInput01 = value01;

        // Kurve formt, Ausgang geklemmt auf [0,1] -- Macro-Ziele haben keine
        // Kapazitaet jenseits des Nominalbereichs (anders als die MPE-Achsen).
        const auto shaped = juce::jlimit (0.0f, 1.0f, binding->curve.apply (value01));
        binding->lastOutput01 = shaped;

        if (binding->target != nullptr)
            binding->target->sendValue (shaped);
    }
}

} // namespace conduit::grid
