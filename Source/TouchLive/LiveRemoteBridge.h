#pragma once

#include <functional>
#include <map>
#include <memory>

#include <juce_osc/juce_osc.h>

#include "AlphaTrackLcd.h"
#include "Core/ControllerProfileLibrary.h"
#include "Core/MidiPortHub.h"
#include "Core/MidiRigSettings.h"
#include "Core/PositionFeedbackRouter.h"
#include "LiveSetModel.h"

namespace conduit
{

//==============================================================================
/** Live-Remote-Bridge (07/2026): macht einen Hardware-Controller (Frontier
    AlphaTrack) zur Ableton-Live-Fernbedienung fuer den in Live SELEKTIERTEN
    Track -- Volume/Pan/Sends auf dem Motorfader, Track-Navigation,
    Mute/Solo/Arm mit LED-Feedback, Track-Name + Song-Position im LCD.

    User-Entscheidungen (17.07.2026):
    - Fader-MODI: Volume (Default) | Pan (PAN-Taste, Toggle) | Send 1-4
      (F1-F4, Toggle) | Send 5-8 (SHIFT + F1-F4). Aktive Modus-Taste erneut
      druecken = zurueck zu Volume. LED der aktiven Modus-Taste an
      (Send 5-8 teilt die F-LED -- die Unterscheidung zeigt das LCD).
    - TRACK</>-Tasten wechseln den selektierten Track IN LIVE
      (/live/song/set/selected_track); die Bridge folgt generell Lives
      Selektion (tracks-Domain-Skalar `selected`).
    - LCD Zeile 1 = Track-Name; Zeile 2 = Song-Position (Takt.Beat),
      bei Fader-Touch/Moduswechsel voruebergehend das aktive Ziel
      (Volume in dB, Pan L/C/R, Send = NAME DES RETURN-TRACKS + Wert).

    Architektur:
    - Headless, Message Thread, Member des EngineProcessor. Takt =
      MidiPortHub::subscribeTick (60 Hz); eigene Hub-Abos auf das
      Live-Remote-Rollen-Geraet (fan-out, konfliktfrei parallel zur
      GridPage).
    - Wert-Beobachtung = TICK-DIFFING gegen lokale Caches (Muster
      PositionFeedbackRouter) -- keine ValueTree-Listener; Diffs nur auf
      Ganzzahlen/Bools/Strings.
    - Motor-Feedback = eigene PositionFeedbackRouter-Instanz
      (`currentBoundValueFor` = Wert des aktiven Bridge-Ziels, touch-gated).
      Der Motor zeigt Lives Wert; waehrend der User schiebt, gewinnt die
      Hand (Touch-Note), das eigene Echo verwirft die Suppression.
    - Rollen-Aufloesung ueber die CSV-CONTROL-IDs des Profils (`fader`,
      `pan`, `f1..f4`, `shift`, `track_l/r`, `mute`, `solo`, `rec_arm`) --
      kein neues role-Vokabular (das behaelt seine M7-Semantik).
    - LCD nur bei Profil-`display == "alphatrack_lcd"`; beim (Re-)Resolve
      geht einmal das Native-Mode-Force-SysEx raus (Applet ueberfluessig).

    KONFLIKTREGEL: traegt dasselbe Geraet zusaetzlich die Grid-Controller-
    Rolle, bleibt die Bridge inaktiv -- sonst konsumieren Grid-Bindungen
    UND Bridge denselben Fader und zwei Motor-Router senden gegeneinander.

    Testbar ueber std::function-Seams (sendMidi/sendCommand/sendTouchValue/
    noteTouched/isLiveConnected/nowMs) + direkte handleNote/-Controller/
    tick-Aufrufe. */
class LiveRemoteBridge : private juce::ChangeListener
{
public:
    LiveRemoteBridge (MidiPortHub& hubToUse,
                      MidiRigSettings& rigSettingsToUse,
                      ControllerProfileLibrary& profileLibraryToUse,
                      LiveSetModel& modelToUse);
    ~LiveRemoteBridge() override;

    //==========================================================================
    // Seams (App-Verdrahtung im EngineProcessor; Tests injizieren Captures)

    std::function<void (const juce::MidiMessage&)> sendMidi;
    std::function<bool (const juce::OSCMessage&)>  sendCommand;
    std::function<void (const juce::OSCMessage&)>  sendTouchValue;
    std::function<void (const juce::String&)>      noteTouched;
    std::function<bool()>                          isLiveConnected;
    std::function<double()>                        nowMs;

    //==========================================================================
    enum class FaderMode { volume, pan, send };

    [[nodiscard]] FaderMode faderMode() const noexcept { return mode; }
    [[nodiscard]] int activeSendIndex() const noexcept { return sendIndex; }
    [[nodiscard]] bool isActive() const noexcept { return active; }

    //==========================================================================
    // Event-Eingaenge [Message Thread] -- die App-Hub-Abos rufen dieselben
    // Methoden; Tests speisen direkt ein.

    void handleNote (const midi::NoteEvent& event);
    void handleController (const midi::ControllerEvent& event);

    /** ~60 Hz (Hub-Tick): Modell-Diffs -> Motor / LEDs / LCD. */
    void tick();

    /** Rolle/Profil aus der Registry neu aufloesen (laeuft automatisch bei
        jedem Registry-Broadcast; public fuer Tests). */
    void refreshFromRegistry();

    // LCD-Override-Dauer nach einem Moduswechsel (User sieht das neue Ziel).
    static constexpr double kOverrideMs = 1500.0;

    // Eigene Fader-Sends gelten fuer Anzeige-Zwecke solange als frisch
    // (die Echo-Suppression haelt das Modell waehrenddessen absichtlich alt).
    static constexpr double kLocalValueFreshMs = 400.0;

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    /** Modus-Taste (PAN/F1-F4): Toggle-Logik + LCD-Override-Fenster. */
    void handleModeKey (FaderMode requested, int requestedSendIndex);

    void handleTrackNavigation (bool forward);
    void toggleTrackFlag (const juce::String& field, const juce::String& address);

    void sendFaderValueToLive (float value01);

    /** Wert des aktiven Ziels aus dem Modell (0..1), < 0 = kein Ziel. */
    [[nodiscard]] float currentTargetValue() const;

    /** Anzeige-Wert: frischer lokaler Send gewinnt ueber das (waehrend der
        Suppression absichtlich alte) Modell. */
    [[nodiscard]] float displayTargetValue() const;

    [[nodiscard]] juce::String selectedTrackKey() const;
    [[nodiscard]] juce::ValueTree selectedMixerItem() const;
    [[nodiscard]] juce::String returnTrackName (int index) const;

    void refreshLeds (const juce::ValueTree& mixerItem);
    void refreshLcd();
    void sendLed (int note, bool on);
    void allLedsOff();

    [[nodiscard]] double now() const { return nowMs != nullptr ? nowMs() : juce::Time::getMillisecondCounterHiRes(); }
    [[nodiscard]] bool connected() const { return isLiveConnected != nullptr && isLiveConnected(); }

    //==========================================================================
    MidiPortHub& hub;
    MidiRigSettings& rigSettings;
    ControllerProfileLibrary& profileLibrary;
    LiveSetModel& model;

    /** Noten-/Kanal-Zuordnung der Bridge-Controls (aus den Profil-IDs). */
    struct ControlNotes
    {
        int faderPbChannel = -1;   // Send-Kanal des Pitch-Bend-Faders
        int faderTouch = -1;
        int pan = -1;
        int f[4] = { -1, -1, -1, -1 };
        int shift = -1;
        int trackL = -1, trackR = -1;
        int mute = -1, solo = -1, arm = -1;
    };

    bool active = false;
    juce::Uuid deviceId;   // Null = Rolle unbesetzt/inaktiv
    int deviceChannel = 1;
    ControlNotes notes;

    midirig::PositionFeedbackRouter motorRouter;
    std::unique_ptr<AlphaTrackLcd> lcd;

    FaderMode mode = FaderMode::volume;
    int sendIndex = -1;
    bool shiftHeld = false;
    bool faderTouched = false;
    double overrideUntilMs = 0.0;

    float  localValue01 = -1.0f;   // letzter selbst gesendeter Fader-Wert
    double localValueAtMs = -1.0e9;

    int controllerToken = -1, noteToken = -1, tickToken = -1;
    std::map<int, bool> ledState;   // Note -> zuletzt gesendeter LED-Zustand

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveRemoteBridge)
};

} // namespace conduit
