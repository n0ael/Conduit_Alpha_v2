#include "PickupLedRouter.h"

#include <cmath>

namespace conduit::midirig
{

namespace
{
    const FeedbackAddress* findFeedback (const ControllerControl& control, const char* meaning)
    {
        for (const auto& fb : control.feedback)
            if (fb.meaning.equalsIgnoreCase (meaning))
                return &fb;

        return nullptr;
    }

    bool hasStatusFeedback (const ControllerControl& control)
    {
        for (const auto& fb : control.feedback)
            if (fb.meaning.startsWithIgnoreCase ("status_"))
                return true;

        return false;
    }

    bool isFaderType (const ControllerControl& control)
    {
        return control.type.equalsIgnoreCase ("fader");
    }
}

void PickupLedRouter::setProfile (const ControllerProfile& profileToUse)
{
    profile = profileToUse;
    hasProfile = true;
}

void PickupLedRouter::clearProfile()
{
    profile = {};
    hasProfile = false;
}

void PickupLedRouter::reset()
{
    lastSent.clear();
    blinkPhases.clear();
    heldNotes.clear();
    columnLayer.clear();
    layerChangeWindow.clear();
}

void PickupLedRouter::setColumnLayer (const juce::String& column, int layer)
{
    if (column.isEmpty())
        return;

    const auto it = columnLayer.find (column);
    const auto changed = it == columnLayer.end() || it->second != layer;
    columnLayer[column] = layer;

    // Ebenen-Wechsel (nicht die Erst-Initialisierung) startet das 16tel-
    // Flackern -- eine unveraenderte Wiederholung (idempotenter Setter,
    // Profil-Reload) NICHT.
    if (changed && it != columnLayer.end())
        layerChangeWindow[column] = kLayerChangeTicks;
}

bool PickupLedRouter::beatBlinkOn (double subdivisionBeats) const noexcept
{
    if (subdivisionBeats <= 0.0)
        return true;

    const auto phase = beatPosition / subdivisionBeats;
    return (phase - std::floor (phase)) < 0.5;   // an in der ersten Haelfte
}

void PickupLedRouter::updatePickupState (const grid::InputAddress& address,
                                         const grid::MidiInBindings::PickupState& state)
{
    // `tracked` = wartend ODER abgeholt-und-gehalten (aligned, nur Shift-Ebenen).
    // Aligned-Eintraege braucht ausschliesslich die Shift-Pad-Anzeige (unten);
    // waitingFor() blendet sie fuer die Gruppen-/Einfach-Mechanismen aus.
    if (state.waiting || state.aligned)
        waiting[address] = state;
    else
        waiting.erase (address);
}

void PickupLedRouter::handleControllerNote (int noteNumber, bool isOn)
{
    if (isOn)
        heldNotes.insert (noteNumber);
    else
        heldNotes.erase (noteNumber);
}

const grid::MidiInBindings::PickupState* PickupLedRouter::waitingFor (
    AddressKind kind, int number) const noexcept
{
    // Kanal-agnostisch (M4b): Profil-Adressen tragen keinen verlaesslichen
    // Kanal, die Warte-Adressen den gelernten -- Matching nur Kind+Nummer.
    const auto wantNote = kind == AddressKind::note;

    for (const auto& entry : waiting)
        if (entry.second.waiting   // aligned-Eintraege sind nur fuer die Shift-Pad-Anzeige
            && entry.first.isNote == wantNote && entry.first.number == number)
            return &entry.second;

    return nullptr;
}

int PickupLedRouter::halfPeriodTicksFor (float distance01) noexcept
{
    const auto t = juce::jlimit (0.0f, 1.0f, distance01 / kBlinkFarDistance);
    const auto period = (float) kBlinkFastTicks + t * (float) (kBlinkSlowTicks - kBlinkFastTicks);
    return juce::jmax (1, (int) std::lround (period));
}

std::map<PickupLedRouter::LedAddress, PickupLedRouter::DesiredLed> PickupLedRouter::computeDesired() const
{
    std::map<LedAddress, DesiredLed> desired;

    if (! hasProfile)
        return desired;

    const auto addressOf = [] (const FeedbackAddress& fb)
    { return LedAddress { fb.kind == AddressKind::note, fb.number }; };

    const auto boundFor = [this] (const ControllerControl& control)
    {
        return isAddressBound != nullptr
               && isAddressBound (control.sendKind == AddressKind::note, control.sendNumber);
    };

    //---- 0. BASIS: Channelstrip-Ebenen-Farbe (M7b) --------------------------
    // Alle Pads einer geebenten Spalte leuchten DAUERHAFT in der Ebenen-Farbe
    // (0 rot=led, 1 gruen=led_layer_c, 2 orange=led_layer_b). Aktives Pad
    // (Echo-Wert > 0, also Toggle/Button an) blinkt im 8tel-Takt; ein laufendes
    // Ebenen-Wechsel-Fenster laesst die ganze Spalte im 16tel-Takt flackern.
    // Bewusst ZUERST -- die momentanen Pickup-/Detail-/Shift-Anzeigen
    // (Mechanismen 1-4) ueberschreiben diese Basis, wo sie greifen.
    for (const auto& [column, layer] : columnLayer)
    {
        const auto* colorMeaning = layer == 0 ? kMeaningLed
                                 : layer == 1 ? kMeaningLedLayerC
                                              : kMeaningLedLayerB;
        const auto columnChanging = layerChangeWindow.count (column) > 0;

        for (const auto& control : profile.controls)
        {
            if (! control.type.equalsIgnoreCase ("pad") || control.group != column)
                continue;

            const auto* red   = findFeedback (control, kMeaningLed);
            const auto* amber = findFeedback (control, kMeaningLedLayerB);
            const auto* green = findFeedback (control, kMeaningLedLayerC);

            // Erst alle Farb-Ebenen aus, dann die aktive Ebenen-Farbe setzen.
            for (const auto* fb : { red, amber, green })
                if (fb != nullptr)
                    desired[addressOf (*fb)] = DesiredLed {};

            const auto* colorFb = findFeedback (control, colorMeaning);
            if (colorFb == nullptr)
                colorFb = red;   // Fallback: Basis-LED
            if (colorFb == nullptr)
                continue;

            // Ist-Zustand des Pads aus dem Echo-Cache (Toggle/Button an?).
            const auto padOn = red != nullptr && lastEchoValueFor != nullptr
                               && lastEchoValueFor (red->kind == AddressKind::note, red->number) > 0;

            const auto on = columnChanging ? beatBlinkOn (kSixteenthBeats)
                          : padOn          ? beatBlinkOn (kEighthBeats)
                                           : true;   // ruhend: solid

            desired[addressOf (*colorFb)] = DesiredLed { false, on ? kLedOn : 0, 0.0f };
        }
    }

    //---- 1./2. Gruppen: Status-LED-Aggregat + momentary Detail-Modus --------
    struct GroupInfo
    {
        const ControllerControl* status = nullptr;
        std::vector<const ControllerControl*> members;
    };
    std::map<juce::String, GroupInfo> groups;

    for (const auto& control : profile.controls)
    {
        if (control.group.isEmpty())
            continue;

        auto& info = groups[control.group];

        if (hasStatusFeedback (control))
            info.status = &control;
        else
            info.members.push_back (&control);
    }

    // Member einer Gruppe MIT Status-Control: led_pickup nur im Detail-Modus.
    std::set<const ControllerControl*> statusGroupMembers;

    for (const auto& groupEntry : groups)
    {
        const auto& info = groupEntry.second;

        // Gruppe ohne Status-Control: Member verhalten sich wie einfache
        // Controls (Fall 3 unten).
        if (info.status == nullptr)
            continue;

        const auto* red   = findFeedback (*info.status, kMeaningStatusRed);
        const auto* amber = findFeedback (*info.status, kMeaningStatusAmber);
        const auto* green = findFeedback (*info.status, kMeaningStatusGreen);

        auto anyBound = false;
        const ControllerControl* worstWaiting = nullptr;   // Fader dominiert (rot)
        const grid::MidiInBindings::PickupState* activeState = nullptr;

        for (const auto* member : info.members)
        {
            statusGroupMembers.insert (member);
            anyBound = anyBound || boundFor (*member);

            const auto* state = waitingFor (member->sendKind, member->sendNumber);
            if (state == nullptr)
                continue;

            if (worstWaiting == nullptr || (isFaderType (*member) && ! isFaderType (*worstWaiting)))
                worstWaiting = member;

            // Dreh-Blink: das gerade bewegte wartende Control steuert die Rate.
            if (state->activeRecently && activeState == nullptr)
                activeState = state;
        }

        // Alle Status-Farben erst aus, dann die Ziel-Farbe drueber (die
        // Adressen sind exklusiv Router-verwaltet, Echo ueberspringt sie).
        for (const auto* fb : { red, amber, green })
            if (fb != nullptr)
                desired[addressOf (*fb)] = DesiredLed {};

        const FeedbackAddress* colorFb = nullptr;
        if (worstWaiting != nullptr)
            colorFb = isFaderType (*worstWaiting) ? red : amber;
        else if (anyBound)
            colorFb = green;

        if (colorFb != nullptr)
        {
            if (worstWaiting != nullptr && activeState != nullptr)
                desired[addressOf (*colorFb)] = DesiredLed { true, 0, activeState->distance01 };
            else
                desired[addressOf (*colorFb)] = DesiredLed { false, kLedOn, 0.0f };
        }

        // Detail-Modus (momentary): Push des Status-Controls gehalten.
        const auto pushHeld = info.status->sendKind == AddressKind::note
                              && heldNotes.count (info.status->sendNumber) > 0;
        if (! pushHeld)
            continue;

        for (const auto* member : info.members)
        {
            const auto* pickupFb = findFeedback (*member, kMeaningLedPickup);
            if (pickupFb == nullptr)
                continue;

            const auto baseAddress = addressOf (*pickupFb);

            // Ziel-Pad aufloesen: die led_pickup-Adresse ist dessen Basis-
            // Note (K1: rot); die gruene Ebene liefert led_layer_c des Pads.
            const FeedbackAddress* greenLayer = nullptr;
            if (const auto* target = profile.findBySendAddress (pickupFb->kind, pickupFb->number))
                greenLayer = findFeedback (*target, "led_layer_c");

            const auto* state = waitingFor (member->sendKind, member->sendNumber);

            if (state != nullptr)                     // wartet: rot distanz-blinkend
            {
                desired[baseAddress] = DesiredLed { true, 0, state->distance01 };
                if (greenLayer != nullptr)
                    desired[addressOf (*greenLayer)] = DesiredLed {};
            }
            else if (boundFor (*member))              // abgeholt: gruen solid
            {
                if (greenLayer != nullptr)
                {
                    desired[addressOf (*greenLayer)] = DesiredLed { false, kLedOn, 0.0f };
                    desired[baseAddress] = DesiredLed {};
                }
                else
                {
                    desired[baseAddress] = DesiredLed { false, kLedOn, 0.0f };
                }
            }
            else                                      // ungebunden: aus
            {
                desired[baseAddress] = DesiredLed {};
                if (greenLayer != nullptr)
                    desired[addressOf (*greenLayer)] = DesiredLed {};
            }
        }
    }

    //---- 3. Einfache led_pickup (ohne Status-Gruppe): blinkt bei waiting ----
    for (const auto& control : profile.controls)
    {
        if (statusGroupMembers.count (&control) > 0)
            continue;

        const auto* pickupFb = findFeedback (control, kMeaningLedPickup);
        if (pickupFb == nullptr)
            continue;

        if (const auto* state = waitingFor (control.sendKind, control.sendNumber))
            desired[addressOf (*pickupFb)] = DesiredLed { true, 0, state->distance01 };
    }

    //---- 4. Shift-Pad-Anzeige: gehaltene Modifier-Pads zeigen die RICHTUNG ---
    // Solide Farbe (kein Blinken, User 15.07.2026): rot = Wert verringern,
    // orange = Wert erhoehen, gruen = gefunden. Der Naeherungswert (Blink)
    // bleibt der Spalten-Status-LED ueberlassen (Mechanismus 1). Angezeigt,
    // sobald das Modifier-Pad gehalten wird und die Position bekannt ist --
    // ein `activeRecently`-Gate wird bewusst NICHT verlangt, damit der Status
    // schon "im Moment des Drueckens" steht.
    for (const auto& waitingEntry : waiting)
    {
        const auto& state = waitingEntry.second;

        if (state.modifiers.empty())
            continue;

        for (const auto& modifier : state.modifiers)
        {
            if (heldNotes.count (modifier.note) == 0)
                continue;

            const auto* pad = profile.findBySendAddress (AddressKind::note, modifier.note);
            if (pad == nullptr)
                continue;

            const auto* redLed   = findFeedback (*pad, "led");
            const auto* amberLed = findFeedback (*pad, "led_layer_b");
            const auto* greenLed = findFeedback (*pad, "led_layer_c");
            if (redLed == nullptr && ! pad->feedback.empty())
                redLed = &pad->feedback.front();   // Fallback: erste Adresse

            // Erst alle Farb-Ebenen des Pads loeschen, dann die aktive setzen
            // (getrennte Notennummern je Farbe -- sonst leuchten mehrere).
            for (const auto* fb : { redLed, amberLed, greenLed })
                if (fb != nullptr)
                    desired[addressOf (*fb)] = DesiredLed {};

            const FeedbackAddress* colorFb = nullptr;
            if (state.aligned)
                colorFb = greenLed != nullptr ? greenLed : redLed;
            else
                colorFb = state.physicalAbove ? redLed
                                              : (amberLed != nullptr ? amberLed : redLed);

            if (colorFb != nullptr)
                desired[addressOf (*colorFb)] = DesiredLed { false, kLedOn, 0.0f };
        }
    }

    return desired;
}

void PickupLedRouter::tick()
{
    // M7b: 16tel-Wechsel-Fenster herunterzaehlen (Basis-Layer bleibt danach).
    for (auto it = layerChangeWindow.begin(); it != layerChangeWindow.end();)
    {
        if (--it->second <= 0)
            it = layerChangeWindow.erase (it);
        else
            ++it;
    }

    auto desired = computeDesired();

    // Verwaiste Blink-Phasen abbauen (Adresse blinkt nicht mehr).
    for (auto it = blinkPhases.begin(); it != blinkPhases.end();)
    {
        const auto entry = desired.find (it->first);
        if (entry == desired.end() || ! entry->second.blink)
            it = blinkPhases.erase (it);
        else
            ++it;
    }

    // Soll-Werte dieses Ticks (Blink-Phasen fortschreiben; Eintritt =
    // sofort sichtbarer erster Puls).
    std::map<LedAddress, int> values;

    for (const auto& [address, led] : desired)
    {
        if (! led.blink)
        {
            values[address] = led.solidValue;
            continue;
        }

        auto [it, inserted] = blinkPhases.try_emplace (address);
        auto& phase = it->second;

        if (! inserted && ++phase.counter >= halfPeriodTicksFor (led.distance01))
        {
            phase.counter = 0;
            phase.on = ! phase.on;
        }

        values[address] = phase.on ? kLedOn : 0;
    }

    // Nicht mehr verwaltete LEDs restaurieren: letzter Echo-Stand, sonst 0
    // (blosses 0-Senden wuerde aktive Toggle-LEDs loeschen).
    for (auto it = lastSent.begin(); it != lastSent.end();)
    {
        if (values.count (it->first) == 0)
        {
            const auto restore = lastEchoValueFor != nullptr
                                     ? lastEchoValueFor (it->first.isNote, it->first.number)
                                     : -1;
            sendValue (it->first, juce::jmax (0, restore));
            it = lastSent.erase (it);
        }
        else
        {
            ++it;
        }
    }

    // Nur Aenderungen senden (Dedupe pro Adresse).
    for (const auto& [address, value] : values)
    {
        const auto it = lastSent.find (address);
        if (it == lastSent.end() || it->second != value)
        {
            sendValue (address, value);
            lastSent[address] = value;
        }
    }
}

void PickupLedRouter::sendValue (const LedAddress& address, int value7bit) const
{
    if (send != nullptr)
        send (address.isNote, address.number, value7bit);
}

} // namespace conduit::midirig
