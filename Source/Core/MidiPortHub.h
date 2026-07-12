#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>

#include "Interfaces/IMidiOutputTarget.h"
#include "MidiControllerEvent.h"
#include "MidiRigSettings.h"
#include "Util/SpscQueue.h"

namespace conduit::midirig
{
    /** Reine Matching-Funktion (ADR 006 E3, Muster CalibrationProfile):
        exakter Name-Treffer zuerst, sonst Prefix-Treffer (ein Suffix wie
        " (2)" auf EINER der beiden Seiten wird ignoriert), sonst leer.
        Kein Zugriff auf Laufzeit-MIDI-APIs — pur und Catch2-testbar. */
    [[nodiscard]] juce::String resolvePortName (const juce::String& registeredName,
                                                 const juce::StringArray& availableNames);

    /** Offener Eingangs-Port (Seam): das reale Handle hält den
        juce::MidiInput (gestartet); Destruktion stoppt und schließt.
        Tests liefern ein Fake-Handle und rufen den registrierten
        MidiInputCallback direkt auf. */
    struct InputPortHandle
    {
        virtual ~InputPortHandle() = default;
    };

    /** Offener Ausgangs-Port (Seam): send() auf dem Message Thread. */
    struct OutputPortHandle
    {
        virtual ~OutputPortHandle() = default;
        virtual void send (const juce::MidiMessage& message) = 0;
    };

    using InputPortOpener = std::function<std::unique_ptr<InputPortHandle> (
        const juce::String& identifier, juce::MidiInputCallback& callback)>;
    using OutputPortOpener = std::function<std::unique_ptr<OutputPortHandle> (
        const juce::String& identifier)>;

    /** Reale Öffner (juce::MidiInput / MidiDeviceTarget). */
    [[nodiscard]] InputPortOpener defaultInputOpener();
    [[nodiscard]] OutputPortOpener defaultOutputOpener();
}

namespace conduit
{

//==============================================================================
/**
    MIDI-Rig-Hub (ADR 006 M1b) — besitzt die offenen MIDI-Ports aller
    registrierten RigDevices und verteilt deren Events.

    Threading (Rule midirig, E4/E7): pro offenem Eingangsport läuft der
    JUCE-MIDI-Callback auf einem EIGENEN System-Thread → jede Verbindung
    hat ihre EIGENE SpscQueue<midi::ControllerEvent> UND
    SpscQueue<midi::NoteEvent> (nie mehrere Producer auf eine Queue). EIN
    zentraler 60-Hz-Timer [Message Thread] entleert alle Queues und ruft
    die Abonnenten. Der Audio-Thread ist NIE beteiligt.

    Überlauf (User-Entscheidung 12.07.2026, „Latest-Pending"): läuft eine
    Controller-Queue voll, wandert das neueste Event in einen atomaren
    Ein-Slot-Puffer (gepackt in ein uint64, neuere überschreiben ältere);
    der Producer schiebt ihn VOR dem nächsten Event nach (Reihenfolge
    bleibt gewahrt), der Drain leert ihn am Tick-Ende (Flut-Abriss). Der
    finale Controller-Wert kommt damit garantiert an; Zwischenwerte einer
    Flut gehen verloren. Note-Queues verwerfen bei Überlauf schlicht das
    neueste Event (M0-Parität — das Echo ist reine Anzeige).

    Ports folgen der Registry: syncFromRegistry() öffnet/schließt anhand
    der aufgelösten Portnamen (exakt→Prefix); Registry-Änderungen
    (ChangeBroadcaster) und USB-Reconnects (MidiDeviceListConnection)
    triggern den Re-Sync automatisch. Nicht auflösbare Ports sind pro
    Gerät der Zustand „nicht verbunden" — kein Fehler-Spam.

    Die Device-Enumeration und die Port-Öffner sind injizierbar (Default =
    echte JUCE-Aufrufe) — der Hub ist ohne Hardware Catch2-testbar.
*/
class MidiPortHub : private juce::Timer,
                    private juce::ChangeListener
{
public:
    using DeviceListProvider = std::function<juce::Array<juce::MidiDeviceInfo>()>;
    using ControllerCallback = std::function<void (const midi::ControllerEvent&)>;
    using NoteCallback       = std::function<void (const midi::NoteEvent&)>;
    using TickCallback       = std::function<void()>;

    explicit MidiPortHub (MidiRigSettings& settingsToUse,
                          DeviceListProvider inputProviderToUse = &juce::MidiInput::getAvailableDevices,
                          DeviceListProvider outputProviderToUse = &juce::MidiOutput::getAvailableDevices,
                          midirig::InputPortOpener inputOpenerToUse = midirig::defaultInputOpener(),
                          midirig::OutputPortOpener outputOpenerToUse = midirig::defaultOutputOpener());
    ~MidiPortHub() override;

    //==========================================================================
    // Device-Listen (M1a)

    /** [Message Thread] Device-Listen neu abfragen (ohne Port-Sync). */
    void refreshAvailableDevices();

    [[nodiscard]] const juce::Array<juce::MidiDeviceInfo>& availableInputs() const noexcept
    {
        return cachedInputs;
    }
    [[nodiscard]] const juce::Array<juce::MidiDeviceInfo>& availableOutputs() const noexcept
    {
        return cachedOutputs;
    }

    [[nodiscard]] juce::String resolveInputFor (const RigDevice& device) const;
    [[nodiscard]] juce::String resolveOutputFor (const RigDevice& device) const;
    [[nodiscard]] juce::String resolveInputForId (const juce::Uuid& id) const;
    [[nodiscard]] juce::String resolveOutputForId (const juce::Uuid& id) const;

    //==========================================================================
    // Portbetrieb (M1b) — alle Methoden Message Thread

    /** Listen aktualisieren + Ports gemäß Registry öffnen/schließen.
        Idempotent; bereits passende offene Ports bleiben unangetastet. */
    void syncFromRegistry();

    [[nodiscard]] bool isInputConnected (const juce::Uuid& deviceId) const noexcept;
    [[nodiscard]] bool isOutputConnected (const juce::Uuid& deviceId) const noexcept;

    /** Name des aktuell offenen Ausgangsports (leer = nicht verbunden). */
    [[nodiscard]] juce::String openOutputNameFor (const juce::Uuid& deviceId) const;

    //==========================================================================
    // Abonnements [Message Thread] — Rückgabe: Token für unsubscribe()

    int subscribeController (const juce::Uuid& deviceId, ControllerCallback callback);
    int subscribeNotes (const juce::Uuid& deviceId, NoteCallback callback);

    /** Feuert nach JEDEM Drain (~60 Hz), auch ohne Events — treibt
        nachgelagerte Glättung (Soft-Takeover/One-Pole, Grid Block G). */
    int subscribeTick (TickCallback callback);

    void unsubscribe (int token);

    //==========================================================================
    // Senden [Message Thread]

    /** No-op, wenn das Gerät keinen offenen Ausgangsport hat. */
    void send (const juce::Uuid& deviceId, const juce::MidiMessage& message);

    /** Stabile IMidiOutputTarget-Fassade pro Gerät (lebt so lange wie der
        Hub); löst den Port bei jedem send() live auf. */
    [[nodiscard]] grid::IMidiOutputTarget& outputTargetFor (const juce::Uuid& deviceId);

    /** Rollen-Fassade „Grid-Ausgang": sendet an das Gerät, das die Registry
        AKTUELL als Grid-Ausgang führt (übersteht Rollen-Wechsel — für
        MpeMidiSink/GridPage/MacroPanel). */
    [[nodiscard]] grid::IMidiOutputTarget& gridOutputTarget() noexcept { return gridOutputFacade; }

    //==========================================================================
    /** [Message Thread] Alle Queues jetzt entleeren + Tick feuern —
        Test-Hook; der 60-Hz-Timer ruft dieselbe Methode. */
    void drainNow();

private:
    //==========================================================================
    /** Verbindung eines RigDevice-EINGANGS: eigener MIDI-System-Thread als
        Producer → eigene Queues (E4). Producer-seitiger Zustand (Pending-
        Slot) wird NUR im MIDI-Callback berührt. */
    struct InputConnection final : juce::MidiInputCallback
    {
        // Reihenfolge: handle als LETZTES Member — sein Destruktor stoppt
        // den Port (und damit den Producer), BEVOR die Queues sterben.
        juce::Uuid deviceId;
        juce::String openName;
        SpscQueue<midi::ControllerEvent> controllerQueue { 512 };
        SpscQueue<midi::NoteEvent> noteQueue { 512 };

        // Latest-Pending-Überlauf: gepacktes ControllerEvent + Present-Bit.
        std::atomic<juce::uint64> overflowSlot { 0 };

        std::unique_ptr<midirig::InputPortHandle> handle;

        void handleIncomingMidiMessage (juce::MidiInput* source,
                                        const juce::MidiMessage& message) override;

    private:
        void pushController (const midi::ControllerEvent& event);
    };

    struct OutputConnection
    {
        juce::Uuid deviceId;
        juce::String openName;
        std::unique_ptr<midirig::OutputPortHandle> handle;
    };

    class DeviceFacade final : public grid::IMidiOutputTarget
    {
    public:
        DeviceFacade (MidiPortHub& hubToUse, const juce::Uuid& idToUse)
            : hub (hubToUse), deviceId (idToUse) {}
        void send (const juce::MidiMessage& message) override { hub.send (deviceId, message); }
        [[nodiscard]] juce::Uuid id() const noexcept { return deviceId; }

    private:
        MidiPortHub& hub;
        juce::Uuid deviceId;
    };

    class GridOutputFacade final : public grid::IMidiOutputTarget
    {
    public:
        explicit GridOutputFacade (MidiPortHub& hubToUse) : hub (hubToUse) {}
        void send (const juce::MidiMessage& message) override
        {
            hub.send (hub.settings.getGridOutputDeviceId(), message);
        }

    private:
        MidiPortHub& hub;
    };

    struct ControllerSubscription { int token; juce::Uuid deviceId; ControllerCallback callback; };
    struct NoteSubscription       { int token; juce::Uuid deviceId; NoteCallback callback; };
    struct TickSubscription       { int token; TickCallback callback; };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    void syncInputs();
    void syncOutputs();
    [[nodiscard]] InputConnection* findInput (const juce::Uuid& deviceId) const noexcept;
    [[nodiscard]] OutputConnection* findOutput (const juce::Uuid& deviceId) const noexcept;
    [[nodiscard]] juce::String identifierForName (const juce::Array<juce::MidiDeviceInfo>& devices,
                                                  const juce::String& name) const;

    void dispatchController (const juce::Uuid& deviceId, const midi::ControllerEvent& event);
    void dispatchNote (const juce::Uuid& deviceId, const midi::NoteEvent& event);

    MidiRigSettings& settings;
    DeviceListProvider inputProvider;
    DeviceListProvider outputProvider;
    midirig::InputPortOpener inputOpener;
    midirig::OutputPortOpener outputOpener;

    juce::Array<juce::MidiDeviceInfo> cachedInputs;
    juce::Array<juce::MidiDeviceInfo> cachedOutputs;

    std::vector<std::unique_ptr<InputConnection>> inputs;
    std::vector<std::unique_ptr<OutputConnection>> outputs;
    std::vector<std::unique_ptr<DeviceFacade>> facades;
    GridOutputFacade gridOutputFacade { *this };

    std::vector<ControllerSubscription> controllerSubscriptions;
    std::vector<NoteSubscription> noteSubscriptions;
    std::vector<TickSubscription> tickSubscriptions;
    int nextToken = 1;

    // USB-Reconnect: System meldet Geräte-Änderungen → Re-Sync [MT].
    juce::MidiDeviceListConnection deviceListConnection;

    static constexpr int kPumpHz = 60;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiPortHub)
};

} // namespace conduit
