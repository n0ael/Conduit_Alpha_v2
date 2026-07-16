#include "MidiInBindings.h"

#include <algorithm>
#include <cmath>

#include "ChannelStripLayers.h"   // M8: decodeSignedDelta (relativeTicks)

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

const juce::String MidiInBindings::kAutoColumn = "\x01__auto__";

void MidiInBindings::setActiveLayer (const juce::String& column, int layer)
{
    if (column.isNotEmpty())
        activeLayerByColumn[column] = layer;
}

int MidiInBindings::activeLayer (const juce::String& column) const noexcept
{
    const auto it = activeLayerByColumn.find (column);
    return it != activeLayerByColumn.end() ? it->second : 0;
}

void MidiInBindings::bind (const MacroControlKey& key, int channel, int cc, bool isNote,
                           ModifierSet modifiers, bool suppressWhileShift,
                           const juce::String& column, int layer)
{
    // M8: der Nummernraum reicht bis 128+16 (Pitch-Bend-Adressen, s.
    // pitchBendBindingNumber) -- alte Sessions kennen keine Nummer > 127.
    const auto clampedChannel = juce::jlimit (1, 16, channel);
    const auto clampedCc      = juce::jlimit (0, kPitchBendBindingBase + 16, cc);

    auto canonical = canonicalize (std::move (modifiers));

    // Eine Note kann nicht ihr eigener Modifier sein.
    if (isNote)
        eraseSorted (canonical, { clampedChannel, clampedCc });

    // M7: Spalte/Ebene aufloesen. kAutoColumn (Live/Learn) -> Profil-Spalte der
    // Adresse + deren aktuell aktive Ebene ("aktive Ebene = Lernziel").
    // Explizite Werte (Persistenz-Load) werden unveraendert uebernommen.
    juce::String resolvedColumn = column;
    int          resolvedLayer  = layer;
    if (column == kAutoColumn)
    {
        resolvedColumn = columnResolver != nullptr
                             ? columnResolver (clampedChannel, clampedCc, isNote)
                             : juce::String();
        resolvedLayer  = resolvedColumn.isNotEmpty() ? activeLayer (resolvedColumn) : -1;
    }

    // Bestehende Bindung desselben Keys UND derselben Adresse MIT identischem
    // Modifier-Set UND identischer Ebene ersetzen -- unterschiedliche Shift-
    // (M5) bzw. Channelstrip-Ebenen (M7) derselben Adresse koexistieren; CC-
    // und Note-Namensraum bleiben getrennt (M4).
    bindings.erase (std::remove_if (bindings.begin(), bindings.end(),
                        [&] (const Binding& b)
                        {
                            return b.key == key
                                   || (b.channel == clampedChannel && b.cc == clampedCc
                                       && b.isNote == isNote && b.modifiers == canonical
                                       && b.column == resolvedColumn && b.layer == resolvedLayer);
                        }),
                    bindings.end());

    Binding binding;
    binding.channel   = clampedChannel;
    binding.cc        = clampedCc;
    binding.isNote    = isNote;
    binding.modifiers = std::move (canonical);
    binding.suppressWhileShift = suppressWhileShift;
    binding.column    = resolvedColumn;
    binding.layer     = resolvedLayer;
    binding.key       = key;
    bindings.push_back (std::move (binding));

    if (onBindingsChanged != nullptr)
        onBindingsChanged();
}

void MidiInBindings::unbind (const MacroControlKey& key)
{
    bindings.erase (std::remove_if (bindings.begin(), bindings.end(),
                        [&] (const Binding& b) { return b.key == key; }),
                    bindings.end());

    if (onBindingsChanged != nullptr)
        onBindingsChanged();
}

void MidiInBindings::setSuppressWhileShift (const MacroControlKey& key, bool shouldSuppress)
{
    for (auto& binding : bindings)
    {
        if (binding.key == key && binding.suppressWhileShift != shouldSuppress)
        {
            binding.suppressWhileShift = shouldSuppress;
            binding.deferredPress01 = -1.0f;   // halber Zustand verfaellt

            if (onBindingsChanged != nullptr)
                onBindingsChanged();
        }
    }
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

    // M8: profil-getriebene Adress-Modi -- Endlos-Encoder (signed Ticks)
    // und Scrub-Stroeme laufen NICHT durch den Absolut-Pfad/Takeover.
    switch (addressModeFor (cc, false))
    {
        case AddressMode::relativeTicks:
            applyRelativeTicks (channel, cc, value7bit);
            return;

        case AddressMode::scrub:
            applyScrub (channel, cc, (float) juce::jlimit (0, 127, value7bit) / 127.0f);
            return;

        case AddressMode::absolute:
        case AddressMode::direct:
            break;
    }

    applyIncoming (channel, cc, false, false,
                   (float) juce::jlimit (0, 127, value7bit) / 127.0f);
}

void MidiInBindings::handleIncomingPitchBend (int channel, int value14)
{
    const auto number = pitchBendBindingNumber (channel);

    // MIDI-Learn: identisch zum CC-Pfad (M5 Chord-Learn) -- der erste
    // Pitch Bend bindet sofort mit den gehaltenen Noten als Shift-Ebene.
    if (learnArmed)
    {
        learnArmed = false;
        hasLearnCandidate = false;
        bind (learnKey, channel, number, false, heldNotes);

        if (onLearnCompleted != nullptr)
            onLearnCompleted (learnKey, channel, number, false, heldNotes);
    }

    const auto value01 = (float) juce::jlimit (0, 16383, value14) / 16383.0f;

    if (addressModeFor (number, false) == AddressMode::scrub)
    {
        applyScrub (channel, number, value01);
        return;
    }

    applyIncoming (channel, number, false, false, value01);
}

//==============================================================================
void MidiInBindings::setAddressMode (int number, bool isNote, AddressMode mode, int relativeSteps,
                                     midirig::RelativeEncoding relEncoding)
{
    if (mode == AddressMode::absolute)
        addressModes.erase ({ number, isNote });
    else
        addressModes[{ number, isNote }] = { mode, juce::jmax (0, relativeSteps), relEncoding };
}

void MidiInBindings::clearAddressModes()
{
    addressModes.clear();
    scrubAnchors.clear();
}

AddressMode MidiInBindings::addressModeFor (int number, bool isNote) const noexcept
{
    const auto it = addressModes.find ({ number, isNote });
    return it != addressModes.end() ? it->second.mode : AddressMode::absolute;
}

const MidiInBindings::Binding* MidiInBindings::activeBindingForAddress (
    int number, bool isNote) const noexcept
{
    const Binding* best = nullptr;

    for (const auto& binding : bindings)
    {
        if (binding.cc != number || binding.isNote != isNote)
            continue;

        if (! std::includes (heldNotes.begin(), heldNotes.end(),
                             binding.modifiers.begin(), binding.modifiers.end()))
            continue;

        if (binding.column.isNotEmpty() && binding.layer != activeLayer (binding.column))
            continue;

        if (best == nullptr || binding.modifiers.size() >= best->modifiers.size())
            best = &binding;
    }

    return best;
}

void MidiInBindings::applyScrub (int channel, int number, float value01)
{
    // Anker-Logik: das erste Event nach einer Pause traegt kein Delta (der
    // Finger setzt neu auf -- kein Sprung); danach zaehlt die Differenz
    // aufeinanderfolgender Positionen (voller Strip = voller Regelweg).
    const InputAddress address { juce::jlimit (1, 16, channel), number, false };

    const auto it = scrubAnchors.find (address);
    const auto recent = it != scrubAnchors.end()
                        && tickCounter - it->second.tick <= kScrubGapTicks;
    const auto previous = recent ? it->second.raw : value01;
    scrubAnchors[address] = { value01, tickCounter };

    auto* binding = bestMatch (channel, number, false);
    if (binding == nullptr)
        return;

    markModifiersUsed (binding->modifiers);
    binding->pendingDelta01 += value01 - previous;
}

void MidiInBindings::applyRelativeTicks (int channel, int number, int value7bit)
{
    // Kodierung ist GERAETEABHAENGIG und kommt aus dem Profil (M8-Feldtest:
    // das AlphaTrack kodiert sign-magnitude -- als Zweierkomplement gelesen
    // wurde aus "1 Tick zurueck" ein Sprung von -63, der Wert fiel auf 0).
    const auto it = addressModes.find ({ number, false });
    const auto encoding = it != addressModes.end() ? it->second.relEncoding
                                                   : midirig::RelativeEncoding::twosComplement;

    const auto delta = midirig::decodeRelativeDelta (value7bit, encoding);
    if (delta == 0)
        return;

    auto* binding = bestMatch (channel, number, false);
    if (binding == nullptr)
        return;

    markModifiersUsed (binding->modifiers);

    const auto steps = it != addressModes.end() && it->second.steps > 0
                           ? it->second.steps : kDefaultRelativeSteps;
    binding->pendingDelta01 += (float) delta / (float) steps;
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

        // M7: eine geebente Bindung matcht nur auf der aktiven Ebene ihrer
        // Spalte (nicht geebent = column leer -> immer gueltig).
        if (binding.column.isNotEmpty() && binding.layer != activeLayer (binding.column))
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
    // M6: geteilte physische Position pro CC-Adresse (roher Wert, vor der
    // Glaettung) -- ueber alle Shift-Ebenen hinweg gibt es nur den EINEN
    // Fader. Noten bleiben aussen vor: momentary, keine Position.
    if (! isNote)
    {
        auto hasAddress = false;
        for (const auto& b : bindings)
            hasAddress = hasAddress || (! b.isNote && b.channel == channel && b.cc == number);

        if (hasAddress)
        {
            physicalPositions[{ channel, number, false }] = value01;
            lastActivityTick[{ channel, number, false }]  = tickCounter;
        }
    }

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

    // M6 Geschwister-Disengage: jedes physische CC-Event widerlegt das
    // Engagement aller NICHT gewaehlten Ebenen derselben Adresse -- behebt
    // den Wertsprung beim Shift-Ebenen-Wechsel (engaged blieb frueher stehen).
    // Nur CCs: ein Note-Toggle muss sein Engagement behalten, sonst
    // verschluckt der Takeover den naechsten Press (Velocity vs. Zustand).
    if (! isNote)
        for (auto& b : bindings)
            if (&b != binding && ! b.isNote && b.channel == channel && b.cc == number)
                b.takeover.disengage();

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
    // M8: akkumulierte Deltas (scrub/relativeTicks) ZUERST anwenden --
    // direkt, ohne Glaettung/Takeover (die Stroeme sind selbst weich, ein
    // Pickup ist fuer relative Eingaben bedeutungslos).
    for (auto& binding : bindings)
    {
        if (juce::exactlyEqual (binding.pendingDelta01, 0.0f))
            continue;

        const auto current = currentValueFor != nullptr ? currentValueFor (binding.key) : 0.0f;
        const auto value   = juce::jlimit (0.0f, 1.0f, current + binding.pendingDelta01);
        binding.pendingDelta01 = 0.0f;
        binding.target01   = value;
        binding.smoothed01 = value;
        binding.pending    = false;

        if (applyValue != nullptr)
            applyValue (binding.key, value);

        if (onFeedbackEcho != nullptr)
            onFeedbackEcho (binding.channel, binding.cc, binding.isNote,
                            currentValueFor != nullptr ? currentValueFor (binding.key) : value);
    }

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

        // M6 Sprung-Modus (pickupEnabled == false): Werte greifen sofort.
        // M8 direct (Motorfader): der Motor steht per Definition richtig --
        // die Adresse durchlaeuft NIE den Takeover.
        const auto direct = addressModeFor (binding.cc, binding.isNote) == AddressMode::direct;
        if (! direct && pickupEnabled && ! binding.takeover.shouldApply (current, binding.smoothed01))
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

    // M6: Warte-Zustaende ERST nach der pending-Schleife bewerten, damit
    // Engagements/Echos dieses Ticks sichtbar sind (kein Flackern beim
    // Uebergang Pickup -> Anzeige).
    updatePickupStates (currentValueFor);
}

void MidiInBindings::setPickupEnabled (bool enabled)
{
    if (pickupEnabled == enabled)
        return;

    pickupEnabled = enabled;

    // Rueckkehr zum Pickup-Modus: Engagements aus der Sprung-Zeit sind
    // bedeutungslos -- alles muss neu aufnehmen.
    if (enabled)
        for (auto& binding : bindings)
            binding.takeover.disengage();
}

void MidiInBindings::updatePickupStates (const std::function<float (const MacroControlKey&)>& currentValueFor)
{
    ++tickCounter;

    if (onPickupStateChanged == nullptr)
        return;

    // Kandidaten: alle gebundenen CC-Adressen plus die zuletzt als wartend
    // gemeldeten (Verwaisten-Abbau nach unbind).
    std::set<InputAddress> candidates;

    for (const auto& binding : bindings)
        if (! binding.isNote)
            candidates.insert ({ binding.channel, binding.cc, false });

    for (const auto& entry : pickupStates)
        candidates.insert (entry.first);

    for (const auto& address : candidates)
    {
        PickupState next;

        // waiting := aktive Ebene existiert, ist nicht engaged, und die
        // BEKANNTE physische Position liegt weiter als kPickupEpsilon vom
        // Software-Wert entfernt. Unbekannte Position wartet nie (Blink =
        // belegter Konflikt, kein Weihnachtsbaum nach App-Start).
        // M8: nur ABSOLUTE Adressen warten -- direct (Motor steht richtig)
        // und scrub/relativeTicks (keine sinnvolle Position) sind exempt.
        if (pickupEnabled && addressModeFor (address.number, address.isNote) == AddressMode::absolute)
            if (auto* active = bestMatch (address.channel, address.number, address.isNote))
                if (const auto physical = physicalPositions.find (address);
                    physical != physicalPositions.end())
                {
                    const auto current = currentValueFor != nullptr
                                             ? currentValueFor (active->key) : 0.0f;
                    const auto delta    = physical->second - current;   // Vorzeichen = Richtung
                    const auto distance = std::abs (delta);

                    next.modifiers     = active->modifiers;
                    next.physicalAbove = delta > 0.0f;   // Knob hoeher -> verringern

                    if (! active->takeover.engaged && distance > kPickupEpsilon)
                    {
                        next.waiting    = true;
                        next.distance01 = distance;
                    }
                    else if (! active->modifiers.empty())
                    {
                        // Abgeholt: gruene Richtungsanzeige NUR fuer Shift-Ebenen
                        // (Basis-Controls brauchen kein Halte-Feedback -> erased).
                        next.aligned = true;
                    }
                }

        if (const auto activity = lastActivityTick.find (address);
            activity != lastActivityTick.end())
            next.activeRecently = tickCounter - activity->second <= kActivityHoldTicks;

        const auto known = pickupStates.find (address);
        const auto prev  = known != pickupStates.end() ? known->second : PickupState{};

        const auto tracked = next.waiting || next.aligned;
        const auto changed = prev.waiting != next.waiting
                             || prev.aligned != next.aligned
                             || (tracked
                                 && (std::abs (prev.distance01 - next.distance01) > kDistanceReportDelta
                                     || prev.activeRecently != next.activeRecently
                                     || prev.physicalAbove != next.physicalAbove
                                     || prev.modifiers != next.modifiers));

        if (changed)
            onPickupStateChanged (address, next);

        if (tracked)
            pickupStates[address] = next;
        else
            pickupStates.erase (address);
    }
}

void MidiInBindings::notifyLocalTouch (const MacroControlKey& key) noexcept
{
    for (auto& binding : bindings)
        if (binding.key == key)
            binding.takeover.disengage();
}

} // namespace conduit::grid
