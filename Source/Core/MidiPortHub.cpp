#include "MidiPortHub.h"

#include <algorithm>

#include "MidiDeviceTarget.h"

namespace conduit::midirig
{

namespace
{
    /** Strippt einen Duplikat-Suffix " (<Ziffern>)" (z. B. "ES-3 (2)" ->
        "ES-3"); Namen ohne diesen Suffix bleiben unverändert. */
    juce::String stripDeviceSuffix (const juce::String& name)
    {
        const auto trimmed = name.trimEnd();
        if (! trimmed.endsWithChar (')'))
            return name;

        const auto openParenIndex = trimmed.lastIndexOfChar ('(');
        if (openParenIndex <= 0)
            return name;

        const auto inner = trimmed.substring (openParenIndex + 1, trimmed.length() - 1);
        if (inner.isEmpty() || ! inner.containsOnly ("0123456789"))
            return name;

        return trimmed.substring (0, openParenIndex).trimEnd();
    }

    struct RealInputHandle final : InputPortHandle
    {
        std::unique_ptr<juce::MidiInput> input;

        ~RealInputHandle() override
        {
            if (input != nullptr)
                input->stop();
        }
    };

    struct RealOutputHandle final : OutputPortHandle
    {
        grid::MidiDeviceTarget target;

        void send (const juce::MidiMessage& message) override { target.send (message); }
    };
}

juce::String resolvePortName (const juce::String& registeredName,
                              const juce::StringArray& availableNames)
{
    if (registeredName.isEmpty())
        return {};

    if (availableNames.contains (registeredName))
        return registeredName;

    const auto registeredPrefix = stripDeviceSuffix (registeredName);
    for (const auto& available : availableNames)
        if (stripDeviceSuffix (available) == registeredPrefix)
            return available;

    return {};
}

InputPortOpener defaultInputOpener()
{
    return [] (const juce::String& identifier, juce::MidiInputCallback& callback)
               -> std::unique_ptr<InputPortHandle>
    {
        auto handle = std::make_unique<RealInputHandle>();
        handle->input = juce::MidiInput::openDevice (identifier, &callback);
        if (handle->input == nullptr)
            return nullptr;

        handle->input->start();
        return handle;
    };
}

OutputPortOpener defaultOutputOpener()
{
    return [] (const juce::String& identifier) -> std::unique_ptr<OutputPortHandle>
    {
        auto handle = std::make_unique<RealOutputHandle>();
        if (! handle->target.openDevice (identifier))
            return nullptr;

        return handle;
    };
}

} // namespace conduit::midirig

namespace conduit
{

namespace
{
    juce::StringArray namesOf (const juce::Array<juce::MidiDeviceInfo>& devices)
    {
        juce::StringArray names;
        for (const auto& device : devices)
            names.add (device.name);
        return names;
    }

    // Latest-Pending-Überlauf: ControllerEvent gepackt in ein uint64 —
    // Present-Bit 63, kind [0..1], channel [2..6], number [7..21],
    // value [22..36], is14Bit [37], isRelative [38]. 14-Bit-Werte passen.
    constexpr juce::uint64 kOverflowPresent = 1ull << 63;

    juce::uint64 packControllerEvent (const midi::ControllerEvent& e) noexcept
    {
        return kOverflowPresent
             | (static_cast<juce::uint64> (static_cast<int> (e.kind)) & 0x3ull)
             | ((static_cast<juce::uint64> (e.channel) & 0x1full) << 2)
             | ((static_cast<juce::uint64> (e.number) & 0x7fffull) << 7)
             | ((static_cast<juce::uint64> (e.value) & 0x7fffull) << 22)
             | ((e.is14Bit ? 1ull : 0ull) << 37)
             | ((e.isRelative ? 1ull : 0ull) << 38);
    }

    midi::ControllerEvent unpackControllerEvent (juce::uint64 packed) noexcept
    {
        midi::ControllerEvent e;
        e.kind       = static_cast<midi::ControllerEvent::Kind> (static_cast<int> (packed & 0x3ull));
        e.channel    = static_cast<int> ((packed >> 2) & 0x1full);
        e.number     = static_cast<int> ((packed >> 7) & 0x7fffull);
        e.value      = static_cast<int> ((packed >> 22) & 0x7fffull);
        e.is14Bit    = ((packed >> 37) & 1ull) != 0;
        e.isRelative = ((packed >> 38) & 1ull) != 0;
        return e;
    }
}

//==============================================================================
void MidiPortHub::InputConnection::handleIncomingMidiMessage (juce::MidiInput*,
                                                              const juce::MidiMessage& message)
{
    // MIDI-SYSTEM-Thread (einziger Producer dieser Queues). M1b: nur CC und
    // Noten — alles andere wird verworfen (NRPN/PC-Assembler folgt in M2,
    // VOR dem Queue-Push, Rule midirig).
    if (message.isController())
    {
        midi::ControllerEvent event;
        event.kind    = midi::ControllerEvent::Kind::cc;
        event.channel = message.getChannel();
        event.number  = message.getControllerNumber();
        event.value   = message.getControllerValue();
        pushController (event);
    }
    else if (message.isNoteOn())
    {
        // Volle Note-Queue verwirft das neue Event (M0-Parität — reine
        // Anzeige, der 60-Hz-Drain entleert schnell genug).
        noteQueue.push ({ message.getNoteNumber(), message.getVelocity(), true });
    }
    else if (message.isNoteOff())
    {
        noteQueue.push ({ message.getNoteNumber(), 0, false });
    }
}

void MidiPortHub::InputConnection::pushController (const midi::ControllerEvent& event)
{
    // Producer-Thread. Erst einen hängengebliebenen Überlauf nachschieben
    // (Reihenfolge bleibt gewahrt), dann das neue Event; scheitert beides,
    // gewinnt das NEUESTE Event den Slot (Latest-Pending).
    if (const auto packed = overflowSlot.exchange (0, std::memory_order_acq_rel); packed != 0)
    {
        if (! controllerQueue.push (unpackControllerEvent (packed)))
        {
            overflowSlot.store (packControllerEvent (event), std::memory_order_release);
            return;
        }
    }

    if (! controllerQueue.push (event))
        overflowSlot.store (packControllerEvent (event), std::memory_order_release);
}

//==============================================================================
MidiPortHub::MidiPortHub (MidiRigSettings& settingsToUse,
                          DeviceListProvider inputProviderToUse,
                          DeviceListProvider outputProviderToUse,
                          midirig::InputPortOpener inputOpenerToUse,
                          midirig::OutputPortOpener outputOpenerToUse)
    : settings (settingsToUse),
      inputProvider (std::move (inputProviderToUse)),
      outputProvider (std::move (outputProviderToUse)),
      inputOpener (std::move (inputOpenerToUse)),
      outputOpener (std::move (outputOpenerToUse))
{
    // TSan-Fund (CI 12.07.2026): der 60-Hz-TimerThread liest den
    // MessageManager-Singleton — der Hub wird im EngineProcessor VOR den
    // Graph-Membern konstruiert, deren rebuild() den Singleton sonst erst
    // lazy erzeugt (Data Race auf den statischen Pointer). Erzeugung hier
    // auf dem Konstruktions-Thread erzwingen, BEVOR Threads starten.
    juce::MessageManager::getInstance();

    settings.addChangeListener (this);

    // USB-Reconnect: das System meldet Geräte-Änderungen auf dem Message
    // Thread — Re-Sync bindet Ports neu (Prefix-Match) bzw. markiert
    // fehlende Geräte als „nicht verbunden".
    deviceListConnection = juce::MidiDeviceListConnection::make ([this] { syncFromRegistry(); });

    startTimerHz (kPumpHz);
}

MidiPortHub::~MidiPortHub()
{
    stopTimer();
    settings.removeChangeListener (this);
    inputs.clear();   // stoppt die Ports (Producer) vor allem Weiteren
    outputs.clear();
}

void MidiPortHub::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &settings)
        syncFromRegistry();
}

void MidiPortHub::timerCallback()
{
    drainNow();
}

//==============================================================================
void MidiPortHub::refreshAvailableDevices()
{
    cachedInputs  = inputProvider  != nullptr ? inputProvider()  : juce::Array<juce::MidiDeviceInfo>();
    cachedOutputs = outputProvider != nullptr ? outputProvider() : juce::Array<juce::MidiDeviceInfo>();
}

juce::String MidiPortHub::resolveInputFor (const RigDevice& device) const
{
    return midirig::resolvePortName (device.midiInName, namesOf (cachedInputs));
}

juce::String MidiPortHub::resolveOutputFor (const RigDevice& device) const
{
    return midirig::resolvePortName (device.midiOutName, namesOf (cachedOutputs));
}

juce::String MidiPortHub::resolveInputForId (const juce::Uuid& id) const
{
    const auto index = settings.indexOfId (id);
    return index >= 0 ? resolveInputFor (settings.getDevice (index)) : juce::String();
}

juce::String MidiPortHub::resolveOutputForId (const juce::Uuid& id) const
{
    const auto index = settings.indexOfId (id);
    return index >= 0 ? resolveOutputFor (settings.getDevice (index)) : juce::String();
}

//==============================================================================
void MidiPortHub::syncFromRegistry()
{
    refreshAvailableDevices();
    syncInputs();
    syncOutputs();
}

void MidiPortHub::syncInputs()
{
    // Soll-Zustand: pro Registry-Gerät der aufgelöste Eingangs-Portname.
    std::vector<std::pair<juce::Uuid, juce::String>> desired;
    for (int i = 0; i < settings.getNumDevices(); ++i)
    {
        const auto device = settings.getDevice (i);
        const auto resolved = resolveInputFor (device);
        if (resolved.isNotEmpty())
            desired.emplace_back (device.id, resolved);
    }

    // Verbindungen schließen, deren Gerät fehlt oder deren Port sich
    // geändert hat (Destruktion stoppt den Port zuerst — Member-Ordnung).
    inputs.erase (std::remove_if (inputs.begin(), inputs.end(),
                      [&desired] (const std::unique_ptr<InputConnection>& connection)
                      {
                          return std::none_of (desired.begin(), desired.end(),
                              [&connection] (const auto& want)
                              {
                                  return want.first == connection->deviceId
                                      && want.second == connection->openName;
                              });
                      }),
                  inputs.end());

    // Fehlende öffnen — scheitert das Öffnen, bleibt das Gerät schlicht
    // „nicht verbunden" (kein Fehler-Spam, nächster Sync versucht es neu).
    for (const auto& [deviceId, portName] : desired)
    {
        if (findInput (deviceId) != nullptr)
            continue;

        const auto identifier = identifierForName (cachedInputs, portName);
        if (identifier.isEmpty() || inputOpener == nullptr)
            continue;

        auto connection = std::make_unique<InputConnection>();
        connection->deviceId = deviceId;
        connection->openName = portName;
        connection->handle = inputOpener (identifier, *connection);

        if (connection->handle != nullptr)
            inputs.push_back (std::move (connection));
    }
}

void MidiPortHub::syncOutputs()
{
    std::vector<std::pair<juce::Uuid, juce::String>> desired;
    for (int i = 0; i < settings.getNumDevices(); ++i)
    {
        const auto device = settings.getDevice (i);
        const auto resolved = resolveOutputFor (device);
        if (resolved.isNotEmpty())
            desired.emplace_back (device.id, resolved);
    }

    outputs.erase (std::remove_if (outputs.begin(), outputs.end(),
                       [&desired] (const std::unique_ptr<OutputConnection>& connection)
                       {
                           return std::none_of (desired.begin(), desired.end(),
                               [&connection] (const auto& want)
                               {
                                   return want.first == connection->deviceId
                                       && want.second == connection->openName;
                               });
                       }),
                   outputs.end());

    for (const auto& [deviceId, portName] : desired)
    {
        if (findOutput (deviceId) != nullptr)
            continue;

        const auto identifier = identifierForName (cachedOutputs, portName);
        if (identifier.isEmpty() || outputOpener == nullptr)
            continue;

        auto handle = outputOpener (identifier);
        if (handle == nullptr)
            continue;

        auto connection = std::make_unique<OutputConnection>();
        connection->deviceId = deviceId;
        connection->openName = portName;
        connection->handle = std::move (handle);
        outputs.push_back (std::move (connection));
    }
}

MidiPortHub::InputConnection* MidiPortHub::findInput (const juce::Uuid& deviceId) const noexcept
{
    for (const auto& connection : inputs)
        if (connection->deviceId == deviceId)
            return connection.get();
    return nullptr;
}

MidiPortHub::OutputConnection* MidiPortHub::findOutput (const juce::Uuid& deviceId) const noexcept
{
    for (const auto& connection : outputs)
        if (connection->deviceId == deviceId)
            return connection.get();
    return nullptr;
}

juce::String MidiPortHub::identifierForName (const juce::Array<juce::MidiDeviceInfo>& devices,
                                             const juce::String& name) const
{
    for (const auto& device : devices)
        if (device.name == name)
            return device.identifier;
    return {};
}

bool MidiPortHub::isInputConnected (const juce::Uuid& deviceId) const noexcept
{
    return findInput (deviceId) != nullptr;
}

bool MidiPortHub::isOutputConnected (const juce::Uuid& deviceId) const noexcept
{
    return findOutput (deviceId) != nullptr;
}

juce::String MidiPortHub::openOutputNameFor (const juce::Uuid& deviceId) const
{
    const auto* connection = findOutput (deviceId);
    return connection != nullptr ? connection->openName : juce::String();
}

//==============================================================================
int MidiPortHub::subscribeController (const juce::Uuid& deviceId, ControllerCallback callback)
{
    const auto token = nextToken++;
    controllerSubscriptions.push_back ({ token, deviceId, std::move (callback) });
    return token;
}

int MidiPortHub::subscribeNotes (const juce::Uuid& deviceId, NoteCallback callback)
{
    const auto token = nextToken++;
    noteSubscriptions.push_back ({ token, deviceId, std::move (callback) });
    return token;
}

int MidiPortHub::subscribeTick (TickCallback callback)
{
    const auto token = nextToken++;
    tickSubscriptions.push_back ({ token, std::move (callback) });
    return token;
}

void MidiPortHub::unsubscribe (int token)
{
    const auto matchesToken = [token] (const auto& subscription)
    { return subscription.token == token; };

    controllerSubscriptions.erase (std::remove_if (controllerSubscriptions.begin(),
                                                   controllerSubscriptions.end(), matchesToken),
                                   controllerSubscriptions.end());
    noteSubscriptions.erase (std::remove_if (noteSubscriptions.begin(),
                                             noteSubscriptions.end(), matchesToken),
                             noteSubscriptions.end());
    tickSubscriptions.erase (std::remove_if (tickSubscriptions.begin(),
                                             tickSubscriptions.end(), matchesToken),
                             tickSubscriptions.end());
}

void MidiPortHub::dispatchController (const juce::Uuid& deviceId, const midi::ControllerEvent& event)
{
    for (std::size_t i = 0; i < controllerSubscriptions.size(); ++i)
        if (controllerSubscriptions[i].deviceId == deviceId && controllerSubscriptions[i].callback != nullptr)
            controllerSubscriptions[i].callback (event);
}

void MidiPortHub::dispatchNote (const juce::Uuid& deviceId, const midi::NoteEvent& event)
{
    for (std::size_t i = 0; i < noteSubscriptions.size(); ++i)
        if (noteSubscriptions[i].deviceId == deviceId && noteSubscriptions[i].callback != nullptr)
            noteSubscriptions[i].callback (event);
}

void MidiPortHub::drainNow()
{
    for (const auto& connection : inputs)
    {
        midi::ControllerEvent controllerEvent;
        while (connection->controllerQueue.pop (controllerEvent))
            dispatchController (connection->deviceId, controllerEvent);

        // Flut-Abriss: liegt noch ein Latest-Pending im Slot, ist es neuer
        // als alles Gequeue-te (der Producer schiebt VOR jedem neuen Event
        // nach) — jetzt ausliefern.
        if (const auto packed = connection->overflowSlot.exchange (0, std::memory_order_acq_rel); packed != 0)
            dispatchController (connection->deviceId, unpackControllerEvent (packed));

        midi::NoteEvent noteEvent;
        while (connection->noteQueue.pop (noteEvent))
            dispatchNote (connection->deviceId, noteEvent);
    }

    for (std::size_t i = 0; i < tickSubscriptions.size(); ++i)
        if (tickSubscriptions[i].callback != nullptr)
            tickSubscriptions[i].callback();
}

//==============================================================================
void MidiPortHub::send (const juce::Uuid& deviceId, const juce::MidiMessage& message)
{
    if (auto* connection = findOutput (deviceId); connection != nullptr && connection->handle != nullptr)
        connection->handle->send (message);
}

grid::IMidiOutputTarget& MidiPortHub::outputTargetFor (const juce::Uuid& deviceId)
{
    for (const auto& facade : facades)
        if (facade->id() == deviceId)
            return *facade;

    facades.push_back (std::make_unique<DeviceFacade> (*this, deviceId));
    return *facades.back();
}

} // namespace conduit
