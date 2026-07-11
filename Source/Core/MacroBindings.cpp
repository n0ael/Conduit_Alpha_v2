#include "MacroBindings.h"

#include <cmath>

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
    return "CC " + juce::String (cc) + " / Kanal " + juce::String (midiChannel);
}

juce::ValueTree MidiCcTarget::toState() const
{
    juce::ValueTree state (kStateType);
    state.setProperty ("channel", midiChannel, nullptr);
    state.setProperty ("cc", cc, nullptr);
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
