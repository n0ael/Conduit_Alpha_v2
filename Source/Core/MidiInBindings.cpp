#include "MidiInBindings.h"

#include <algorithm>
#include <cmath>

namespace conduit::grid
{

void MidiInBindings::bind (const MacroControlKey& key, int channel, int cc)
{
    const auto clampedChannel = juce::jlimit (1, 16, channel);
    const auto clampedCc      = juce::jlimit (0, 127, cc);

    // Bestehende Bindung desselben Keys UND desselben (channel, cc) ersetzen
    // -- ein CC steuert genau ein Control, ein Control hoert auf genau
    // einen CC.
    bindings.erase (std::remove_if (bindings.begin(), bindings.end(),
                        [&] (const Binding& b)
                        {
                            return b.key == key
                                   || (b.channel == clampedChannel && b.cc == clampedCc);
                        }),
                    bindings.end());

    Binding binding;
    binding.channel = clampedChannel;
    binding.cc      = clampedCc;
    binding.key     = key;
    bindings.push_back (binding);
}

void MidiInBindings::unbind (const MacroControlKey& key)
{
    bindings.erase (std::remove_if (bindings.begin(), bindings.end(),
                        [&] (const Binding& b) { return b.key == key; }),
                    bindings.end());
}

const MidiInBindings::Binding* MidiInBindings::bindingFor (const MacroControlKey& key) const noexcept
{
    for (const auto& binding : bindings)
        if (binding.key == key)
            return &binding;

    return nullptr;
}

void MidiInBindings::armLearn (const MacroControlKey& key) noexcept
{
    learnArmed = true;
    learnKey = key;
}

void MidiInBindings::handleIncomingCc (int channel, int cc, int value7bit)
{
    // MIDI-Learn: der erste eingehende CC bindet und wird dann normal
    // weiterverarbeitet (die neue Bindung nimmt den Wert direkt auf).
    if (learnArmed)
    {
        learnArmed = false;
        bind (learnKey, channel, cc);

        if (onLearnCompleted != nullptr)
            onLearnCompleted (learnKey, channel, cc);
    }

    const auto value01 = (float) juce::jlimit (0, 127, value7bit) / 127.0f;

    for (auto& binding : bindings)
    {
        if (binding.channel != channel || binding.cc != cc)
            continue;

        binding.target01 = value01;

        // Glaettungs-Startpunkt: beim allerersten Wert direkt dort beginnen
        // (kein Anfahren von 0).
        if (binding.smoothed01 < 0.0f)
            binding.smoothed01 = value01;

        binding.pending = true;
    }
}

void MidiInBindings::tick (const std::function<float (const MacroControlKey&)>& currentValueFor,
                           const std::function<void (const MacroControlKey&, float)>& applyValue)
{
    for (auto& binding : bindings)
    {
        if (! binding.pending || binding.target01 < 0.0f)
            continue;

        // One-Pole-Glaettung Richtung Zielwert (127-Stufen-CC → weiche Fahrt);
        // nahe genug = einrasten und Ruhe geben.
        const auto distance = binding.target01 - binding.smoothed01;
        if (std::abs (distance) <= kSmoothingSnap)
        {
            binding.smoothed01 = binding.target01;
            binding.pending = false;
        }
        else
        {
            binding.smoothed01 += distance * kSmoothingPerTick;
        }

        const auto current = currentValueFor != nullptr ? currentValueFor (binding.key) : 0.0f;

        if (! binding.takeover.shouldApply (current, binding.smoothed01))
            continue;   // Pickup noch nicht erreicht -- kein Parametersprung

        if (applyValue != nullptr)
            applyValue (binding.key, binding.smoothed01);

        if (onFeedbackEcho != nullptr)
            onFeedbackEcho (binding.channel, binding.cc, binding.smoothed01);
    }
}

void MidiInBindings::notifyLocalTouch (const MacroControlKey& key) noexcept
{
    for (auto& binding : bindings)
        if (binding.key == key)
            binding.takeover.disengage();
}

} // namespace conduit::grid
