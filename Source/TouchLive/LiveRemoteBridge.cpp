#include "LiveRemoteBridge.h"

#include "LiveFaderScale.h"
#include "TouchLiveClient.h"

namespace conduit
{

namespace
{
    /** Control eines Profils per CSV-ID (case-insensitiv), nullptr = fehlt. */
    const midirig::ControllerControl* findControl (const midirig::ControllerProfile& profile,
                                                   const char* controlId)
    {
        for (const auto& control : profile.controls)
            if (control.id.equalsIgnoreCase (controlId))
                return &control;

        return nullptr;
    }

    int noteOf (const midirig::ControllerProfile& profile, const char* controlId)
    {
        const auto* control = findControl (profile, controlId);
        return control != nullptr && control->sendKind == midirig::AddressKind::note
                   ? control->sendNumber : -1;
    }

    /** Pan-Anzeige im Live-Stil: C in der Mitte, sonst L/R + 0..50. */
    juce::String panText (float value01)
    {
        const auto pan = juce::roundToInt ((value01 * 2.0f - 1.0f) * 50.0f);
        if (pan == 0)
            return "C";
        return (pan < 0 ? "L" : "R") + juce::String (std::abs (pan));
    }
}

//==============================================================================
LiveRemoteBridge::LiveRemoteBridge (MidiPortHub& hubToUse,
                                    MidiRigSettings& rigSettingsToUse,
                                    ControllerProfileLibrary& profileLibraryToUse,
                                    LiveSetModel& modelToUse)
    : hub (hubToUse),
      rigSettings (rigSettingsToUse),
      profileLibrary (profileLibraryToUse),
      model (modelToUse)
{
    // Motor-Feedback: zweite PositionFeedbackRouter-Instanz (M8-Baustein) --
    // Quelle ist das AKTIVE Bridge-Ziel statt einer MIDI-Bindung.
    motorRouter.currentBoundValueFor = [this] (int, bool) { return currentTargetValue(); };
    motorRouter.send = [this] (const midirig::FeedbackAddress& address, int value14)
    {
        if (sendMidi == nullptr)
            return;

        const auto clamped = juce::jlimit (0, 16383, value14);

        switch (address.kind)
        {
            case midirig::AddressKind::pitchBend:
                sendMidi (juce::MidiMessage::pitchWheel (address.channel, clamped));
                break;

            case midirig::AddressKind::cc:
                sendMidi (juce::MidiMessage::controllerEvent (deviceChannel, address.number,
                                                              clamped >> 7));
                break;

            case midirig::AddressKind::note:
                sendMidi (juce::MidiMessage::noteOn (deviceChannel, address.number,
                                                     (juce::uint8) (clamped >> 7)));
                break;
        }
    };

    rigSettings.addChangeListener (this);
    refreshFromRegistry();
}

LiveRemoteBridge::~LiveRemoteBridge()
{
    rigSettings.removeChangeListener (this);
    hub.unsubscribe (controllerToken);
    hub.unsubscribe (noteToken);
    hub.unsubscribe (tickToken);
}

void LiveRemoteBridge::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &rigSettings)
        refreshFromRegistry();
}

//==============================================================================
void LiveRemoteBridge::refreshFromRegistry()
{
    const auto newDeviceId = rigSettings.getLiveRemoteDeviceId();

    // KONFLIKTREGEL: Grid- und Live-Rolle auf demselben Geraet -> inaktiv
    // (sonst konsumieren Grid-Bindungen UND Bridge denselben Fader und zwei
    // Motor-Router senden gegeneinander).
    const auto conflict = newDeviceId == rigSettings.getGridControllerDeviceId()
                          && ! newDeviceId.isNull();

    const auto index = rigSettings.indexOfId (newDeviceId);
    const midirig::ControllerProfile* profile = nullptr;
    RigDevice device;

    if (! conflict && index >= 0)
    {
        device = rigSettings.getDevice (index);
        if (device.controllerProfileName.isNotEmpty())
            profile = profileLibrary.find (device.controllerProfileName);
    }

    const auto nowActive = profile != nullptr;
    const auto deviceChanged = newDeviceId != deviceId;

    // Abos folgen der Rolle (idempotent bei jedem Registry-Broadcast).
    hub.unsubscribe (controllerToken);
    hub.unsubscribe (noteToken);
    controllerToken = -1;
    noteToken = -1;

    active = nowActive;
    deviceId = newDeviceId;
    deviceChannel = index >= 0 ? device.midiChannel : 1;
    notes = {};

    if (deviceChanged)
    {
        motorRouter.reset();     // Dedupe-Stand gilt nur pro Geraet
        ledState.clear();
        if (lcd != nullptr)
            lcd->forceRedraw();
    }

    if (! active)
    {
        motorRouter.clearProfile();
        lcd.reset();
        return;
    }

    // Rollen-Aufloesung ueber die CSV-Control-IDs (Konvention docs/MidiRig.md).
    if (const auto* fader = findControl (*profile, "fader");
        fader != nullptr && fader->sendKind == midirig::AddressKind::pitchBend)
    {
        notes.faderPbChannel = fader->sendChannel;
        notes.faderTouch     = fader->touchNumber;
    }

    notes.pan    = noteOf (*profile, "pan");
    notes.f[0]   = noteOf (*profile, "f1");
    notes.f[1]   = noteOf (*profile, "f2");
    notes.f[2]   = noteOf (*profile, "f3");
    notes.f[3]   = noteOf (*profile, "f4");
    notes.shift  = noteOf (*profile, "shift");
    notes.trackL = noteOf (*profile, "track_l");
    notes.trackR = noteOf (*profile, "track_r");
    notes.mute   = noteOf (*profile, "mute");
    notes.solo   = noteOf (*profile, "solo");
    notes.arm    = noteOf (*profile, "rec_arm");

    motorRouter.setProfile (*profile);

    // LCD nur bei entsprechender Profil-Faehigkeit; das Native-Mode-Force-
    // SysEx macht das Treiber-Applet ueberfluessig (einmal pro Resolve).
    if (profile->display.equalsIgnoreCase ("alphatrack_lcd"))
    {
        if (lcd == nullptr)
            lcd = std::make_unique<AlphaTrackLcd> ([this] (const juce::MidiMessage& m)
                                                   { if (sendMidi != nullptr) sendMidi (m); });
        lcd->forceRedraw();

        if (sendMidi != nullptr)
            sendMidi (AlphaTrackLcd::nativeModeForce());
    }
    else
    {
        lcd.reset();
    }

    // App-Verdrahtung der Hub-Abos (Tests rufen handleNote/-Controller direkt).
    controllerToken = hub.subscribeController (deviceId,
        [this] (const midi::ControllerEvent& event) { handleController (event); });
    noteToken = hub.subscribeNotes (deviceId,
        [this] (const midi::NoteEvent& event) { handleNote (event); });

    if (tickToken < 0)
        tickToken = hub.subscribeTick ([this] { tick(); });
}

//==============================================================================
void LiveRemoteBridge::handleNote (const midi::NoteEvent& event)
{
    if (! active)
        return;

    // Fader-Touch: Motor-Gate + LCD-Override (die Hand gewinnt).
    if (event.note == notes.faderTouch && notes.faderTouch >= 0)
    {
        motorRouter.handleControllerNote (event.note, event.isOn);
        faderTouched = event.isOn;
        return;
    }

    if (event.note == notes.shift && notes.shift >= 0)
    {
        shiftHeld = event.isOn;
        return;
    }

    if (! event.isOn)
        return;   // Buttons sind momentary -- nur der Press zaehlt

    if (event.note == notes.pan && notes.pan >= 0)
    {
        handleModeKey (FaderMode::pan, -1);
        return;
    }

    for (int i = 0; i < 4; ++i)
    {
        if (event.note == notes.f[i] && notes.f[i] >= 0)
        {
            handleModeKey (FaderMode::send, i + (shiftHeld ? 4 : 0));
            return;
        }
    }

    if (event.note == notes.trackL && notes.trackL >= 0)
    {
        handleTrackNavigation (false);
        return;
    }

    if (event.note == notes.trackR && notes.trackR >= 0)
    {
        handleTrackNavigation (true);
        return;
    }

    if (event.note == notes.mute && notes.mute >= 0)
    {
        toggleTrackFlag ("mute", "/live/track/set/mute");
        return;
    }

    if (event.note == notes.solo && notes.solo >= 0)
    {
        toggleTrackFlag ("solo", "/live/track/set/solo");
        return;
    }

    if (event.note == notes.arm && notes.arm >= 0)
        toggleTrackFlag ("arm", "/live/track/set/arm");
}

void LiveRemoteBridge::handleModeKey (FaderMode requested, int requestedSendIndex)
{
    // Toggle-Semantik: die aktive Modus-Taste erneut = zurueck zu Volume.
    const auto isCurrent = mode == requested
                           && (requested != FaderMode::send || sendIndex == requestedSendIndex);

    mode      = isCurrent ? FaderMode::volume : requested;
    sendIndex = mode == FaderMode::send ? requestedSendIndex : -1;

    // Lokale Anzeige-Werte gehoeren zum alten Ziel.
    localValue01 = -1.0f;
    localValueAtMs = -1.0e9;

    // LCD zeigt das neue Ziel kurz an; der Motor faehrt im naechsten Tick
    // von selbst auf den neuen Zielwert (Router-Diff).
    overrideUntilMs = now() + kOverrideMs;
}

void LiveRemoteBridge::handleController (const midi::ControllerEvent& event)
{
    if (! active || event.kind != midi::ControllerEvent::Kind::pitchBend
        || event.channel != notes.faderPbChannel)
        return;

    sendFaderValueToLive ((float) juce::jlimit (0, 16383, event.value) / 16383.0f);
}

//==============================================================================
void LiveRemoteBridge::sendFaderValueToLive (float value01)
{
    if (! connected() || sendTouchValue == nullptr)
        return;

    const auto key = selectedTrackKey();
    if (key.isEmpty())
        return;

    // Anzeige folgt SOFORT dem eigenen Send (Feel-Regel 5.1) -- das Modell
    // bleibt waehrend der Echo-Suppression absichtlich alt.
    localValue01 = juce::jlimit (0.0f, 1.0f, value01);
    localValueAtMs = now();

    switch (mode)
    {
        case FaderMode::volume:
        {
            juce::OSCMessage message { "/live/track/set/volume" };
            message.addString (key);
            message.addFloat32 (localValue01);
            sendTouchValue (message);
            if (noteTouched != nullptr)
                noteTouched (TouchLiveClient::makeParameterKey ("mixer", key, "vol"));
            break;
        }

        case FaderMode::pan:
        {
            juce::OSCMessage message { "/live/track/set/panning" };
            message.addString (key);
            message.addFloat32 (localValue01 * 2.0f - 1.0f);
            sendTouchValue (message);
            if (noteTouched != nullptr)
                noteTouched (TouchLiveClient::makeParameterKey ("mixer", key, "pan"));
            break;
        }

        case FaderMode::send:
        {
            // Send-Index >= vorhandene Sends: inert (LCD zeigt "-").
            const auto item = selectedMixerItem();
            const auto* sends = item.getProperty ("sends").getArray();
            if (sends == nullptr || sendIndex < 0 || sendIndex >= sends->size())
                return;

            juce::OSCMessage message { "/live/track/set/send" };
            message.addString (key);
            message.addInt32 (sendIndex);
            message.addFloat32 (localValue01);
            sendTouchValue (message);
            if (noteTouched != nullptr)
                noteTouched (TouchLiveClient::makeParameterKey ("mixer", key, "sends"));
            break;
        }
    }
}

void LiveRemoteBridge::handleTrackNavigation (bool forward)
{
    if (! connected() || sendCommand == nullptr)
        return;

    // Geordnete Liste der REGULAEREN Tracks (Returns/Master sind ueber die
    // Stable-ID-Aufloesung der Gegenseite nicht selektierbar).
    struct Entry { juce::String key; int index = 0; };
    std::vector<Entry> tracks;

    auto domain = model.getDomain ("tracks");
    for (const auto& item : domain)
    {
        const auto kind = item.getProperty ("kind").toString();
        if (kind == "audio" || kind == "midi")
            tracks.push_back ({ item.getProperty (touchlive::id::itemKey).toString(),
                                (int) item.getProperty ("index", 0) });
    }

    if (tracks.empty())
        return;

    std::sort (tracks.begin(), tracks.end(),
               [] (const Entry& a, const Entry& b) { return a.index < b.index; });

    const auto selected = selectedTrackKey();
    int position = -1;
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].key == selected)
            position = (int) i;

    // Selektion ist Return/Master/unbekannt: an den Anfang bzw. das Ende.
    int target = 0;
    if (position >= 0)
        target = juce::jlimit (0, (int) tracks.size() - 1, position + (forward ? 1 : -1));
    else
        target = forward ? 0 : (int) tracks.size() - 1;

    if (position >= 0 && target == position)
        return;   // am Ende geklemmt -- kein Send

    juce::OSCMessage message { "/live/song/set/selected_track" };
    message.addString (tracks[(size_t) target].key);
    sendCommand (message);
}

void LiveRemoteBridge::toggleTrackFlag (const juce::String& field, const juce::String& address)
{
    if (! connected() || sendCommand == nullptr)
        return;

    const auto key = selectedTrackKey();
    const auto item = selectedMixerItem();
    if (key.isEmpty() || ! item.isValid() || ! item.hasProperty (field))
        return;   // Track ohne diese Faehigkeit (z. B. arm auf Gruppen) -- no-op

    const auto current = (bool) item.getProperty (field, false);

    juce::OSCMessage message { address };
    message.addString (key);
    message.addInt32 (current ? 0 : 1);   // Int, nie Bool (OSC-Codec-Falle)
    sendCommand (message);
}

//==============================================================================
juce::String LiveRemoteBridge::selectedTrackKey() const
{
    return model.getDomain ("tracks")
        .getProperty ("selected").toString();
}

juce::ValueTree LiveRemoteBridge::selectedMixerItem() const
{
    const auto key = selectedTrackKey();
    if (key.isEmpty())
        return {};

    return model.findItem ("mixer", key);
}

juce::String LiveRemoteBridge::returnTrackName (int index) const
{
    auto domain = model.getDomain ("tracks");
    for (const auto& item : domain)
        if (item.getProperty ("kind").toString() == "return"
            && (int) item.getProperty ("index", -1) == index)
            return item.getProperty ("name").toString();

    return {};
}

float LiveRemoteBridge::currentTargetValue() const
{
    if (! connected())
        return -1.0f;

    const auto item = selectedMixerItem();
    if (! item.isValid())
        return -1.0f;

    switch (mode)
    {
        case FaderMode::volume:
            return item.hasProperty ("vol")
                       ? juce::jlimit (0.0f, 1.0f, (float) (double) item.getProperty ("vol"))
                       : -1.0f;

        case FaderMode::pan:
            return item.hasProperty ("pan")
                       ? juce::jlimit (0.0f, 1.0f,
                                       ((float) (double) item.getProperty ("pan") + 1.0f) * 0.5f)
                       : -1.0f;

        case FaderMode::send:
        {
            const auto* sends = item.getProperty ("sends").getArray();
            if (sends == nullptr || sendIndex < 0 || sendIndex >= sends->size())
                return -1.0f;
            return juce::jlimit (0.0f, 1.0f, (float) (double) (*sends)[sendIndex]);
        }
    }

    return -1.0f;
}

float LiveRemoteBridge::displayTargetValue() const
{
    if (localValue01 >= 0.0f && now() - localValueAtMs <= kLocalValueFreshMs)
        return localValue01;

    return currentTargetValue();
}

//==============================================================================
void LiveRemoteBridge::tick()
{
    if (! active)
        return;

    const auto mixerItem = selectedMixerItem();

    motorRouter.tick();
    refreshLeds (mixerItem);
    refreshLcd();
}

void LiveRemoteBridge::refreshLeds (const juce::ValueTree& mixerItem)
{
    if (! connected())
    {
        allLedsOff();
        return;
    }

    // Modus-LEDs: aktive Taste an (Send 5-8 teilt die F-LED, LCD zeigt den
    // Unterschied); Shift-LED spiegelt das Halten.
    sendLed (notes.pan, mode == FaderMode::pan);
    for (int i = 0; i < 4; ++i)
        sendLed (notes.f[i], mode == FaderMode::send && (sendIndex % 4) == i);
    sendLed (notes.shift, shiftHeld);

    // Track-Zustaende folgen dem MODELL (Live ist die Wahrheit), nie dem
    // Tastendruck; fehlendes Feld (Gruppe ohne arm) = LED aus.
    sendLed (notes.mute, mixerItem.isValid() && (bool) mixerItem.getProperty ("mute", false));
    sendLed (notes.solo, mixerItem.isValid() && (bool) mixerItem.getProperty ("solo", false));
    sendLed (notes.arm,  mixerItem.isValid() && (bool) mixerItem.getProperty ("arm", false));
}

void LiveRemoteBridge::refreshLcd()
{
    if (lcd == nullptr)
        return;

    if (! connected())
    {
        lcd->setLine (0, "Live offline");
        lcd->setLine (1, "");
        lcd->tick();
        return;
    }

    // Zeile 1: Name des selektierten Tracks.
    const auto key = selectedTrackKey();
    auto trackName = juce::String ("-");
    if (key.isNotEmpty())
    {
        const auto trackItem = model.findItem ("tracks", key);
        if (trackItem.isValid())
            trackName = trackItem.getProperty ("name").toString();
    }
    lcd->setLine (0, trackName);

    // Zeile 2: aktives Ziel (bei Touch/Moduswechsel), sonst Song-Position.
    if (faderTouched || now() < overrideUntilMs)
    {
        const auto value = displayTargetValue();

        switch (mode)
        {
            case FaderMode::volume:
                lcd->setLine (1, value >= 0.0f
                                     ? "Vol " + touchlive::faderscale::dbText (value) + " dB"
                                     : juce::String ("Vol -"));
                break;

            case FaderMode::pan:
                lcd->setLine (1, value >= 0.0f ? "Pan " + panText (value)
                                               : juce::String ("Pan -"));
                break;

            case FaderMode::send:
            {
                const auto name = returnTrackName (sendIndex);
                const auto label = name.isNotEmpty()
                                       ? name
                                       : "Send " + juce::String (sendIndex + 1);
                lcd->setLine (1, value >= 0.0f
                                     ? label.substring (0, 11).paddedRight (' ', 11) + " "
                                           + juce::String (juce::roundToInt (value * 100.0f)) + "%"
                                     : label + ": -");
                break;
            }
        }
    }
    else
    {
        // Song-Position aus der transport-Domain (bar/beat, beat-quantisiert).
        auto transport = model.getDomain ("transport");
        if (transport.hasProperty ("bar"))
            lcd->setLine (1, juce::String ((int) transport.getProperty ("bar", 1)) + "."
                                 + juce::String ((int) transport.getProperty ("beat", 1)));
        else
            lcd->setLine (1, "-");
    }

    lcd->tick();
}

void LiveRemoteBridge::sendLed (int note, bool on)
{
    if (note < 0 || sendMidi == nullptr)
        return;

    const auto it = ledState.find (note);
    if (it != ledState.end() && it->second == on)
        return;

    ledState[note] = on;
    sendMidi (juce::MidiMessage::noteOn (deviceChannel, note, (juce::uint8) (on ? 127 : 0)));
}

void LiveRemoteBridge::allLedsOff()
{
    sendLed (notes.pan, false);
    for (int i = 0; i < 4; ++i)
        sendLed (notes.f[i], false);
    sendLed (notes.shift, false);
    sendLed (notes.mute, false);
    sendLed (notes.solo, false);
    sendLed (notes.arm, false);
}

} // namespace conduit
