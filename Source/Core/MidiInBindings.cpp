#include "MidiInBindings.h"

#include <algorithm>
#include <cmath>

namespace conduit::grid
{

namespace
{
    /** Kanonische Form: sortiert, duplikatfrei, Werte geklemmt. */
    ModifierSet canonicalize (ModifierSet modifiers)
    {
        for (auto& m : modifiers)
        {
            m.channel = juce::jlimit (1, 16, m.channel);
            m.note    = juce::jlimit (0, 127, m.note);
        }

        std::sort (modifiers.begin(), modifiers.end());
        modifiers.erase (std::unique (modifiers.begin(), modifiers.end()), modifiers.end());
        return modifiers;
    }

    bool contains (const ModifierSet& sortedSet, const ModifierNote& note) noexcept
    {
        return std::binary_search (sortedSet.begin(), sortedSet.end(), note);
    }

    void insertSorted (ModifierSet& sortedSet, const ModifierNote& note)
    {
        const auto it = std::lower_bound (sortedSet.begin(), sortedSet.end(), note);
        if (it == sortedSet.end() || *it != note)
            sortedSet.insert (it, note);
    }

    void eraseSorted (ModifierSet& sortedSet, const ModifierNote& note)
    {
        const auto it = std::lower_bound (sortedSet.begin(), sortedSet.end(), note);
        if (it != sortedSet.end() && *it == note)
            sortedSet.erase (it);
    }
}

void MidiInBindings::bind (const MacroControlKey& key, int channel, int cc, bool isNote,
                           ModifierSet modifiers, bool suppressWhileShift)
{
    const auto clampedChannel = juce::jlimit (1, 16, channel);
    const auto clampedCc      = juce::jlimit (0, 127, cc);

    auto canonical = canonicalize (std::move (modifiers));

    // Eine Note kann nicht ihr eigener Modifier sein.
    if (isNote)
        eraseSorted (canonical, { clampedChannel, clampedCc });

    // Bestehende Bindung desselben Keys UND derselben Adresse MIT identischem
    // Modifier-Set ersetzen -- unterschiedliche Shift-Ebenen derselben
    // Adresse koexistieren (M5); CC- und Note-Namensraum bleiben getrennt (M4).
    bindings.erase (std::remove_if (bindings.begin(), bindings.end(),
                        [&] (const Binding& b)
                        {
                            return b.key == key
                                   || (b.channel == clampedChannel && b.cc == clampedCc
                                       && b.isNote == isNote && b.modifiers == canonical);
                        }),
                    bindings.end());

    Binding binding;
    binding.channel   = clampedChannel;
    binding.cc        = clampedCc;
    binding.isNote    = isNote;
    binding.modifiers = std::move (canonical);
    binding.suppressWhileShift = suppressWhileShift;
    binding.key       = key;
    bindings.push_back (std::move (binding));
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
    hasLearnCandidate = false;
}

void MidiInBindings::cancelLearn() noexcept
{
    learnArmed = false;
    hasLearnCandidate = false;
}

void MidiInBindings::handleIncomingCc (int channel, int cc, int value7bit)
{
    // MIDI-Learn (M5 Chord-Learn): der erste eingehende CC bindet sofort --
    // mit den gerade gehaltenen Noten als Shift-Ebene (Pad halten + Fader
    // bewegen). Danach wird er normal weiterverarbeitet (die neue Bindung
    // nimmt den Wert direkt auf).
    if (learnArmed)
    {
        learnArmed = false;
        hasLearnCandidate = false;
        bind (learnKey, channel, cc, false, heldNotes);

        if (onLearnCompleted != nullptr)
            onLearnCompleted (learnKey, channel, cc, false, heldNotes);
    }

    applyIncoming (channel, cc, false, false,
                   (float) juce::jlimit (0, 127, value7bit) / 127.0f);
}

void MidiInBindings::handleIncomingNote (int channel, int note, int velocity7bit, bool isOn)
{
    const ModifierNote incoming { juce::jlimit (1, 16, channel), juce::jlimit (0, 127, note) };

    if (isOn)
    {
        // MIDI-Learn (M5): Note-On bindet nicht mehr sofort, sondern wird
        // Kandidat -- die zuvor gehaltenen Noten sind seine Modifier. Kommt
        // stattdessen ein CC, werden ALLE gehaltenen Noten (inkl. dieser)
        // dessen Shift-Ebene; werden alle Noten losgelassen, bindet der
        // Kandidat (Pad-Akkord-Bindung). Ein streunendes Off loest nie aus.
        if (learnArmed)
        {
            learnCandidate = incoming;
            learnCandidateModifiers = heldNotes;
            hasLearnCandidate = true;
        }

        insertSorted (heldNotes, incoming);
    }
    else
    {
        eraseSorted (heldNotes, incoming);

        if (learnArmed && hasLearnCandidate && heldNotes.empty())
        {
            learnArmed = false;
            hasLearnCandidate = false;
            bind (learnKey, learnCandidate.channel, learnCandidate.note, true,
                  learnCandidateModifiers);

            if (onLearnCompleted != nullptr)
                onLearnCompleted (learnKey, learnCandidate.channel, learnCandidate.note, true,
                                  learnCandidateModifiers);
        }
    }

    // Momentary + Velocity (User-Entscheidung 14.07.2026): On = Velocity/127,
    // Off = 0. Toggle-Verhalten macht das Ziel-Control selbst
    // (applyExternalValue schaltet Toggles auf steigender Flanke um).
    applyIncoming (channel, note, true, isOn,
                   isOn ? (float) juce::jlimit (0, 127, velocity7bit) / 127.0f : 0.0f);

    // Shift-Buchhaltung erst NACH dem Off-Routing raeumen (das Off musste
    // noch wissen, ob die Note als Shift gedient hat).
    if (! isOn)
        eraseSorted (usedAsShift, incoming);
}

MidiInBindings::Binding* MidiInBindings::bestMatch (int channel, int number, bool isNote) noexcept
{
    Binding* best = nullptr;

    for (auto& binding : bindings)
    {
        if (binding.channel != channel || binding.cc != number || binding.isNote != isNote)
            continue;

        if (! std::includes (heldNotes.begin(), heldNotes.end(),
                             binding.modifiers.begin(), binding.modifiers.end()))
            continue;

        // Exakteste Ebene gewinnt; bei Gleichstand die zuletzt gebundene.
        if (best == nullptr || binding.modifiers.size() >= best->modifiers.size())
            best = &binding;
    }

    return best;
}

void MidiInBindings::markModifiersUsed (const ModifierSet& modifiers)
{
    for (const auto& m : modifiers)
        insertSorted (usedAsShift, m);
}

void MidiInBindings::setBindingTarget (Binding& binding, float value01)
{
    binding.target01 = value01;

    // Glaettungs-Startpunkt: beim allerersten Wert direkt dort beginnen
    // (kein Anfahren von 0).
    if (binding.smoothed01 < 0.0f)
        binding.smoothed01 = value01;

    binding.pending = true;
}

void MidiInBindings::applyIncoming (int channel, int number, bool isNote, bool noteOn, float value01)
{
    if (isNote && ! noteOn)
    {
        // Note-Off geht an die Ebene(n), die das On gewaehlt hat -- nicht an
        // die aktuell passendste (der User darf Modifier vor dem Pad
        // loslassen, ohne dass die Basis-Ebene faelschlich die 0 bekommt).
        auto latched = false;

        for (auto& binding : bindings)
        {
            if (! binding.noteHeld
                || binding.channel != channel || binding.cc != number || ! binding.isNote)
                continue;

            binding.noteHeld = false;
            latched = true;

            if (binding.suppressWhileShift)
            {
                const ModifierNote self { binding.channel, binding.cc };
                const auto served = contains (usedAsShift, self);
                const auto press  = binding.deferredPress01;
                binding.deferredPress01 = -1.0f;

                if (served || press < 0.0f)
                    continue;   // als Shift gedient -> Eigenfunktion still

                // Aufgeschobener Press als Puls: erst der Press-Wert, nach
                // dem Einrasten zurueck auf 0 (tick()). Der User hat das
                // physische Pad bewusst gedrueckt -- Pickup erzwingen, sonst
                // blockiert der Takeover den Puls (Control 0 -> Press 1).
                binding.takeover.engaged = true;
                setBindingTarget (binding, press);
                binding.pulseRelease = true;
                continue;
            }

            setBindingTarget (binding, 0.0f);
        }

        // Kein On hat diese Adresse gelatcht (z. B. erstes Event ueberhaupt
        // ist ein Off): an die passendste Ebene routen -- seedet die
        // Glaettung bei 0 und laesst den Takeover natuerlich aufnehmen
        // (M4-Verhalten der Erst-Beruehrung).
        if (! latched)
            if (auto* fallback = bestMatch (channel, number, true))
                setBindingTarget (*fallback, 0.0f);

        return;
    }

    auto* binding = bestMatch (channel, number, isNote);
    if (binding == nullptr)
        return;

    markModifiersUsed (binding->modifiers);

    if (isNote)
    {
        binding->noteHeld = true;

        if (binding->suppressWhileShift)
        {
            // Release-Heuristik: Press aufschieben -- ob das Pad in diesem
            // Halten als Shift dient, weiss erst das Off.
            binding->deferredPress01 = value01;
            return;
        }
    }

    setBindingTarget (*binding, value01);
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

            // Puls-Press (suppressWhileShift): nach dem Einrasten des
            // Press-Werts in der naechsten Runde auf 0 zurueck.
            if (binding.pulseRelease)
            {
                binding.pulseRelease = false;
                binding.target01 = 0.0f;
                binding.pending = true;
            }
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

        // Echo spiegelt den IST-Zustand des Controls NACH dem Anwenden (M4b):
        // ein Toggle bleibt an, auch wenn der Eingangswert (Note-Off) auf 0
        // faellt -- die Pad-LED soll den Toggle-Zustand zeigen, nicht den
        // rohen Eingang.
        if (onFeedbackEcho != nullptr)
            onFeedbackEcho (binding.channel, binding.cc, binding.isNote,
                            currentValueFor != nullptr ? currentValueFor (binding.key)
                                                       : binding.smoothed01);
    }
}

void MidiInBindings::notifyLocalTouch (const MacroControlKey& key) noexcept
{
    for (auto& binding : bindings)
        if (binding.key == key)
            binding.takeover.disengage();
}

} // namespace conduit::grid
