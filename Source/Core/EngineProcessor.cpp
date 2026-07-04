#include "EngineProcessor.h"

#include "EngineEditor.h"
#include "Util/RtAllocationGuard.h"
#include "Util/ScaleQuantizer.h"

namespace conduit
{

namespace
{
    /** Settings-Umleitung (Test-Injektionspunkt): ein absoluter folderName
        gewinnt in juce::File::getChildFile über das AppData-Verzeichnis —
        dasselbe Muster nutzen die Settings-Unit-Tests bereits einzeln.
        Ungültiges File → Options unverändert (Produktions-Pfade). */
    juce::PropertiesFile::Options redirectSettings (juce::PropertiesFile::Options options,
                                                    const juce::File& folder)
    {
        if (folder != juce::File())
            options.folderName = folder.getFullPathName();

        return options;
    }
}

EngineProcessor::EngineProcessor()
    : EngineProcessor (juce::File())
{
}

EngineProcessor::EngineProcessor (const juce::File& settingsFolder)
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      rootState (id::root),
      captureSettings  (redirectSettings (CaptureSettings::defaultOptions(),  settingsFolder)),
      channelNames     (redirectSettings (ChannelNames::defaultOptions(),     settingsFolder)),
      meterSettings    (redirectSettings (MeterSettings::defaultOptions(),    settingsFolder)),
      uiSettings       (redirectSettings (UiSettings::defaultOptions(),       settingsFolder)),
      moduleUiDefaults (redirectSettings (ModuleUiDefaults::defaultOptions(), settingsFolder)),
      transportSettings (redirectSettings (TransportSettings::defaultOptions(), settingsFolder)),
      oscSendSettings  (redirectSettings (OscSendSettings::defaultOptions(),  settingsFolder))
{
    // Schema 6.2 — die drei Top-Level-Container des Root-Trees
    rootState.appendChild (juce::ValueTree (id::nodes),               nullptr);
    rootState.appendChild (juce::ValueTree (id::connections),         nullptr);
    rootState.appendChild (juce::ValueTree (id::calibrationProfiles), nullptr);

    registerDefaultModules (moduleFactory);

    // Graph-I/O-Nodes (Konstruktor läuft auf dem Message Thread).
    // Bewusst keine Verbindung Input→Output — kein Mic-Passthrough.
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    audioInputNode  = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioInputNode));
    audioOutputNode = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioOutputNode));

    // Als externe Endpunkte verfügbar machen: Tree-Nodes mit diesen
    // moduleIds mappen auf die I/O-Prozessoren statt auf Factory-Module
    graphManager.registerExternalEndpoint (audioInputModuleId,  audioInputNode->nodeID);
    graphManager.registerExternalEndpoint (audioOutputModuleId, audioOutputNode->nodeID);
    ensureIONodeStates();

    // Takt-Verteiler — IClockSlaves bekommen den Bus bei der Materialisierung
    graphManager.setClockBus (&clockBus);

    // Link-Audio-Kontext — ILinkAudioClients (Send-Module) bekommen Clock +
    // moduleId bei der Materialisierung (7.2)
    graphManager.setLinkClock (&linkClock);

    // Capture-Kontext — ICaptureTapClients (Tap-Module) bekommen Service +
    // moduleId bei der Materialisierung (Spurname == moduleId)
    graphManager.setCaptureService (&captureService);

    // Master-Output-Tap (Looper B2): die Session-Summe nach dem GraphFader
    // als zwei virtuelle Capture-Kanäle — Quelle des Retro-Loopers, und
    // Capture All exportiert die Summe automatisch sample-aligned mit.
    // Registrierung VOR jedem Audio-Start, Handles danach unveränderlich —
    // deshalb schlichte Member statt der rtSlot-Atomics des CaptureTapModule.
    masterTapLeft  = captureService.registerVirtualChannel ("master_l");
    masterTapRight = captureService.registerVirtualChannel ("master_r");

    // Kanal-Namen — Auto-Naming der Send-Kanäle (7.2): Quelle am audio_input
    // liefert ihr ChannelNames-Label
    graphManager.setChannelNames (&channelNames);

    // Modul-Typ-Defaults des Dev-Modus (4.6) — Overlay bei Neu-Anlagen
    graphManager.setModuleUiDefaults (&moduleUiDefaults);

    // Globale Session-Skala (6.2): Defaults sicherstellen, Properties spiegeln
    ensureSessionScaleDefaults();
    rootState.addListener (this);

    // Clip-Reset-Modus in die LevelMeter spiegeln (Start + bei Änderung)
    meterSettings.addChangeListener (this);
    applyMeterSettings();

    // Transport-Settings in die LinkClock spiegeln (Start/Stop-Sync,
    // Clock-Offset) — Start + bei jeder Änderung aus dem Link-Menü
    transportSettings.addChangeListener (this);
    applyTransportSettings();

    // Eingebetteter Input-Link-Send (7.2): Zustand lebt in channelNames —
    // jeder Broadcast (Enable-Toggle, Pairing, Label-Rename, Device-Wechsel
    // via setActiveDevice) zieht den Diff nach
    inputLinkSend.setLinkClock (&linkClock);
    channelNames.addChangeListener (this);

    // Resize-Policy der Capture-Settings gegen den Service verdrahten
    // (Aktivitäts-Check, Invalidierung, Reallokation — CaptureSettings-Doku)
    captureSettings.setBufferHost (&captureService);

    // Export-Dateinamen nutzen die ChannelNames-Labels (eine Quelle für
    // CapturePanel, Dateinamen und Port-Beschriftung) — sanitiert; leeres
    // Ergebnis fällt im Service auf "in{N}" zurück
    captureService.hardwareTrackName = [this] (int channel)
    {
        return ChannelNames::sanitizeFileLabel (
            channelNames.getLabel (ChannelNames::Direction::input, channel), {});
    };

    // Echo-Suppression des Send-Pfads (7.3): jeder in den Tree übernommene
    // Empfangswert impft den Diff-Cache — kein Rücksenden eigener Empfänge
    oscController.onRemoteValueApplied = [this] (const juce::String& nodeUuid,
                                                 const juce::String& parameterId,
                                                 float value)
    {
        oscSendService.noteRemoteValue (nodeUuid, parameterId, value);
    };

    // /conduit/sync (7.3): Client fordert den kompletten Ist-Zustand an
    oscController.onSyncRequested = [this] { oscSendService.sendFullDump(); };

    // /conduit/announce (7.4): find-or-create + Werte-Dump des Nodes an
    // reiche Clients (das Minimal-Max-Device hat keinen Receiver — für den
    // Re-Announce-Fall trotzdem korrekt, neue Nodes deckt der Diff-Tick ab)
    oscController.onAnnounce = [this] (const osc::AnnounceInfo& info)
    {
        const auto node = remoteModuleBinder.handleAnnounce (info);

        if (node.isValid())
            oscSendService.sendNodeValues (node.getProperty (id::nodeId).toString());
    };
}

EngineProcessor::~EngineProcessor()
{
    channelNames.removeChangeListener (this);
    meterSettings.removeChangeListener (this);
    transportSettings.removeChangeListener (this);
    rootState.removeListener (this);
}

//==============================================================================
void EngineProcessor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &meterSettings)
        applyMeterSettings();
    else if (source == &transportSettings)
        applyTransportSettings();
    else if (source == &channelNames)
        rebuildInputSends();
}

void EngineProcessor::applyTransportSettings()
{
    linkClock.setStartStopSyncEnabled (transportSettings.isStartStopSyncEnabled());
    linkClock.setClockOffsetMs (transportSettings.getClockOffsetMs());
    metronome.setEnabled (transportSettings.isMetronomeEnabled());
    metronome.setAnchor (transportSettings.getMetronomeAnchor());
    looperEngine.setAnchor (transportSettings.getLooperAnchor());
}

juce::Result EngineProcessor::commitLooper (int bars)
{
    return looperEngine.commit (bars, captureService,
                                looperLeftIndex, looperRightIndex, barAnchors);
}

void EngineProcessor::setLooperAnchor (int pairIndex)
{
    transportSettings.setLooperAnchor (pairIndex);
    looperEngine.setAnchor (transportSettings.getLooperAnchor());
}

//==============================================================================
void EngineProcessor::setLooperSource (const juce::String& sourceKey)
{
    transportSettings.setLooperSource (sourceKey);
    applyLooperSourceArming();
}

void EngineProcessor::applyLooperSourceArming()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Vorherige Quelle entwaffnen — die normale Gate-Detektion übernimmt
    // dort wieder (offenes Material schließt regulär über den Hold, B1)
    captureService.setChannelArmed (looperLeftIndex, false);
    captureService.setChannelArmed (looperRightIndex, false);
    looperLeftIndex  = -1;
    looperRightIndex = -1;

    const auto key = transportSettings.getLooperSource();

    if (key == "master")
    {
        // Master-Output-Tap (B2): Registry-Slots 0/1; captureIndex ist −1,
        // solange kein Puffersatz die Slots trägt (vor prepare)
        looperLeftIndex  = captureService.getVirtualChannelUiInfo (0).captureIndex;
        looperRightIndex = captureService.getVirtualChannelUiInfo (1).captureIndex;
    }
    else if (key.startsWith ("hw:"))
    {
        // Hardware-Eingangs-Paar 2n/2n+1 — nur Kanäle, die der aktuelle
        // Puffersatz tatsächlich trägt
        const auto pair = key.substring (3).getIntValue();
        const auto channels = captureService.getRingNumChannels();
        const auto leftChannel = pair * 2;

        if (leftChannel < channels)
            looperLeftIndex = leftChannel;
        if (leftChannel + 1 < channels)
            looperRightIndex = leftChannel + 1;
    }
    else if (key.startsWith ("tap:"))
    {
        // Capture-Tap eines Moduls: Basisname → _l/_r-Paar (Stereo) bzw.
        // exakter Name (Mono-Tap)
        const auto baseName = key.substring (4);

        for (int slot = 0; slot < CaptureService::MAX_VIRTUAL_CHANNELS; ++slot)
        {
            const auto info = captureService.getVirtualChannelUiInfo (slot);
            if (! info.inUse || info.captureIndex < 0)
                continue;

            if (info.name == baseName + "_l" || info.name == baseName)
                looperLeftIndex = info.captureIndex;
            else if (info.name == baseName + "_r")
                looperRightIndex = info.captureIndex;
        }
    }

    // Mono-Quelle: rechts folgt links (Commit/Waveform lesen beide Seiten)
    if (looperRightIndex < 0)
        looperRightIndex = looperLeftIndex;

    captureService.setChannelArmed (looperLeftIndex, true);
    if (looperRightIndex != looperLeftIndex)
        captureService.setChannelArmed (looperRightIndex, true);

    // Waveform-Binner folgt der Quelle (Reset + Backfill im Audio Thread, B4)
    looperWaveformTap.setSource (looperLeftIndex, looperRightIndex);
}

void EngineProcessor::rebuildInputSends()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Hardware-Eingangszahl aus dem audio_in-Tree-Node (trägt Ausgangs-Ports,
    // von syncHardwareIOChannels an die echte Device-Zahl gekoppelt)
    auto nodesTree = rootState.getChildWithName (id::nodes);
    auto inNode = nodesTree.getChildWithProperty (id::factoryId, juce::String (audioInputModuleId));
    if (! inNode.isValid())
        inNode = nodesTree.getChildWithProperty (id::moduleId, juce::String (audioInputModuleId));

    const auto channels = inNode.isValid()
                        ? (int) inNode.getProperty (id::numOutputChannels, 0)
                        : 0;

    inputLinkSend.applySends (InputLinkSend::buildSpecs (channelNames, channels));
}

void EngineProcessor::applyMeterSettings()
{
    const auto hold = meterSettings.getClipHoldSeconds();
    inputLevels.setClipHoldSeconds (hold);
    outputLevels.setClipHoldSeconds (hold);
}

//==============================================================================
void EngineProcessor::ensureSessionScaleDefaults()
{
    if (! rootState.hasProperty (id::scaleRoot))
        rootState.setProperty (id::scaleRoot, 0, nullptr);

    if (! rootState.hasProperty (id::scaleType))
        rootState.setProperty (id::scaleType, toString (ScaleType::chromatic), nullptr);

    if (! rootState.hasProperty (id::globalSwing))
        rootState.setProperty (id::globalSwing, 0.0, nullptr);

    refreshScaleAtomics();
}

void EngineProcessor::refreshScaleAtomics()
{
    scaleRootAtomic.store (juce::jlimit (0, 11, (int) rootState.getProperty (id::scaleRoot, 0)),
                           std::memory_order_relaxed);
    scaleTypeAtomic.store (static_cast<int> (scaleTypeFromString (
                               rootState.getProperty (id::scaleType).toString())),
                           std::memory_order_relaxed);
    globalSwingAtomic.store (juce::jlimit (0.0, 0.75,
                                 (double) rootState.getProperty (id::globalSwing, 0.0)),
                             std::memory_order_relaxed);
}

void EngineProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == rootState && (property == id::scaleRoot || property == id::scaleType
                              || property == id::globalSwing))
        refreshScaleAtomics();
}

//==============================================================================
void EngineProcessor::ensureIONodeStates()
{
    auto nodesTree = rootState.getChildWithName (id::nodes);

    const auto ensure = [&nodesTree] (const char* factoryKey, const char* defaultName,
                                      int numInputs, int numOutputs, int x, int y)
    {
        // Vorhanden? factoryId-Match; Alt-Bestände tragen den Schlüssel in moduleId
        if (nodesTree.getChildWithProperty (id::factoryId, juce::String (factoryKey)).isValid()
            || nodesTree.getChildWithProperty (id::moduleId, juce::String (factoryKey)).isValid())
            return;

        juce::ValueTree node (id::node);
        node.setProperty (id::nodeId,            juce::Uuid().toString(),          nullptr);
        node.setProperty (id::type,              toString (ModuleType::io),        nullptr);
        node.setProperty (id::factoryId,         factoryKey,                       nullptr);
        node.setProperty (id::moduleId,          defaultName,                      nullptr);
        node.setProperty (id::stateVersion,      1,                                nullptr);
        node.setProperty (id::nodeState,         toString (NodeState::active),     nullptr);
        node.setProperty (id::nodeError,         juce::String(),                   nullptr);
        node.setProperty (id::positionX,         x,                                nullptr);
        node.setProperty (id::positionY,         y,                                nullptr);
        node.setProperty (id::numInputChannels,  numInputs,                        nullptr);
        node.setProperty (id::numOutputChannels, numOutputs,                       nullptr);
        node.appendChild (juce::ValueTree (id::parameters), nullptr);

        nodesTree.appendChild (node, nullptr);  // Grundausstattung — kein Undo
    };

    // Aus Graph-Sicht: der Input-Prozessor LIEFERT Kanäle (Outputs),
    // der Output-Prozessor NIMMT Kanäle entgegen (Inputs)
    ensure (audioInputModuleId,  "audio_in",  0, 2, 40,  260);
    ensure (audioOutputModuleId, "audio_out", 2, 0, 700, 260);
}

void EngineProcessor::syncHardwareIOChannels (int deviceInputs, int deviceOutputs)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto ins  = juce::jmax (0, deviceInputs);
    const auto outs = juce::jmax (0, deviceOutputs);

    auto nodesTree = rootState.getChildWithName (id::nodes);

    const auto ioNodeFor = [&nodesTree] (const char* factoryKey)
    {
        auto node = nodesTree.getChildWithProperty (id::factoryId, juce::String (factoryKey));
        if (! node.isValid())
            node = nodesTree.getChildWithProperty (id::moduleId, juce::String (factoryKey));
        return node;
    };

    auto inNode  = ioNodeFor (audioInputModuleId);
    auto outNode = ioNodeFor (audioOutputModuleId);

    // Kanalzahl setzen — idempotent (nur bei Abweichung, sonst unnötige
    // UI-Rebuilds pro Gerätewechsel). Input-Prozessor LIEFERT Kanäle →
    // Ausgangs-Ports am audio_in-Node; Output-Prozessor NIMMT Kanäle →
    // Eingangs-Ports am audio_out-Node. Geräte-getrieben → kein Undo.
    if (inNode.isValid() && (int) inNode.getProperty (id::numOutputChannels, -1) != ins)
        inNode.setProperty (id::numOutputChannels, ins, nullptr);

    if (outNode.isValid() && (int) outNode.getProperty (id::numInputChannels, -1) != outs)
        outNode.setProperty (id::numInputChannels, outs, nullptr);

    // Schritt C: Kabel auf verschwundene I/O-Kanäle kappen (kleineres
    // Interface / Ausstecken), damit keine Phantom-Connections zurückbleiben
    if (inNode.isValid())
        pruneEndpointConnections (inNode.getProperty (id::nodeId).toString(),  true,  ins);

    if (outNode.isValid())
        pruneEndpointConnections (outNode.getProperty (id::nodeId).toString(), false, outs);

    // Input-Link-Sends an die neue Kanalzahl anpassen (Schrumpfen retired
    // die betroffenen Kanäle; buildSpecs sieht nur noch gültige Anker)
    rebuildInputSends();
}

void EngineProcessor::pruneEndpointConnections (const juce::String& nodeId, bool asSource, int validChannels)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto connections = rootState.getChildWithName (id::connections);

    // Rückwärts iterieren — removeChild verschiebt die nachfolgenden Indizes
    for (int i = connections.getNumChildren() - 1; i >= 0; --i)
    {
        const auto conn = connections.getChild (i);

        const auto endpointId = (asSource ? conn.getProperty (id::sourceNodeId)
                                          : conn.getProperty (id::destNodeId)).toString();
        if (endpointId != nodeId)
            continue;

        const int channel = (int) (asSource ? conn.getProperty (id::sourceChannel, 0)
                                            : conn.getProperty (id::destChannel, 0));

        if (channel >= validChannels)
            connections.removeChild (i, nullptr);  // geräte-getrieben → kein Undo
    }
}

//==============================================================================
void EngineProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    graph.setPlayConfigDetails (getTotalNumInputChannels(),
                                getTotalNumOutputChannels(),
                                sampleRate, samplesPerBlock);
    graph.prepareToPlay (sampleRate, samplesPerBlock);
    graphFader.prepare (sampleRate);
    linkClock.prepare (sampleRate);
    metronome.prepare (sampleRate);
    captureService.prepare (sampleRate, samplesPerBlock, getTotalNumInputChannels());
    barAnchors.reset();  // SampleClock-Reset invalidiert alle Anker-Positionen

    // Looper-Quelle neu auflösen: der frische Puffersatz vergibt die
    // Capture-Indizes der virtuellen Slots neu (B3); Waveform-Binner und
    // Loop-Playback verwerfen ihren Stand (SampleClock-Reset, B4/B5)
    looperWaveformTap.prepare (sampleRate);
    looperEngine.prepare (sampleRate);
    applyLooperSourceArming();
    inputLevels.prepare  (sampleRate, getTotalNumInputChannels());
    outputLevels.prepare (sampleRate, getTotalNumOutputChannels());
    inputLinkSend.prepare (samplesPerBlock);
}

void EngineProcessor::releaseResources()
{
    graphFader.reset();  // unprepared Fader → GraphManager swappt ohne Fade
    graph.releaseResources();

    // SPSC-Consumer-Wechsel ist nur bei gestopptem Callback zulässig: der
    // Message Thread springt gleich als Consumer der oscToAudioQueue ein —
    // die Annahme "Audio steht" wird hier explizit geprüft statt angenommen.
    JUCE_ASSERT_MESSAGE_THREAD
    jassert (! audioCallbackActive.load (std::memory_order_acquire));

    // Audio steht — der Message Thread darf als Consumer einspringen und
    // verwirft liegengebliebene OSC-Updates. Schließt das Lebensdauer-Fenster
    // der target-Pointer, falls Module bei gestopptem Audio zerstört werden.
    ParameterUpdate discarded;
    while (oscToAudioQueue.pop (discarded)) {}
}

void EngineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Callback-aktiv-Marker für den Consumer-Wechsel-Guard in
    // releaseResources() — Eintritt hier, Austritt am Ende des Blocks
    audioCallbackActive.store (true, std::memory_order_release);

    // Capture-Input-Tap als ERSTE Operation: hier liegt noch der rohe
    // Hardware-Input im Buffer — der Graph überschreibt ihn gleich mit
    // Modul-Outputs und der GraphFader blendet bei Swaps; beides gehört
    // nicht in Metering und (spätere) Aufzeichnung.
    {
        // RT-Audit (Dev-Builds): der Tap muss allocation-free sein — jede
        // new/delete in diesem Abschnitt zählt als Violation und hält unter
        // dem Debugger sofort an (Util/RtAllocationGuard)
        const rt::ScopedRealtimeSection rtAudit;
        captureService.processInputTap (buffer, getTotalNumInputChannels());
        inputLevels.process (buffer, getTotalNumInputChannels());  // Sicht-Metering In
    }

    // Pfad 1 des OSC-Dual-State (6.1): Queue VOR dem Graph vollständig
    // dränieren — lock-free, allocation-free, < 1ms vom Empfang bis hier
    ParameterUpdate update;
    while (oscToAudioQueue.pop (update))
        if (update.target != nullptr)
            update.target->store (update.value, std::memory_order_relaxed);

    // Session-Kontext einmal pro Block einfangen — die IClockSlaves im
    // Graph lesen den Bus im selben Callback (4.2)
    clockBus.current = linkClock.captureClockState (buffer.getNumSamples());
    clockBus.current.scaleRootNote  = scaleRootAtomic.load (std::memory_order_relaxed);
    clockBus.current.scaleTypeIndex = scaleTypeAtomic.load (std::memory_order_relaxed);
    clockBus.current.globalSwing    = globalSwingAtomic.load (std::memory_order_relaxed);

    // Takt-Anker (Looper B1): Taktgrenzen-Überquerungen dieses Blocks
    // sample-genau festhalten. Die SampleClock hat am Tap-Ende bereits
    // weitergetickt — der Block-Start liegt numSamples zurück (Kontrakt
    // CaptureService.h). Allocation-free (Atomics-Ring).
    {
        const auto clockNow = captureService.getSampleClock().now();
        const auto blockSamples = static_cast<std::uint64_t> (buffer.getNumSamples());
        if (clockNow >= blockSamples)
            barAnchors.process (clockBus.current, clockNow - blockSamples,
                                buffer.getNumSamples());
    }

    // Eingebetteter Input-Link-Send (7.2): NACH captureClockState (der Commit
    // braucht den SessionState-Stash dieses Blocks) und VOR dem Graph (der
    // Buffer trägt hier noch den rohen Hardware-Input). Allocation-free.
    {
        const rt::ScopedRealtimeSection rtAudit;
        inputLinkSend.processBlock (buffer, getTotalNumInputChannels(), clockBus.current);
    }

    graph.processBlock (buffer, midiMessages);
    graphFader.process (buffer);  // Master-Fade hinter dem Graph (5.2)

    // Master-Output-Tap (Looper B2): die Session-Summe direkt nach dem
    // Fader in die virtuellen Capture-Kanäle spiegeln — VOR Metronom und
    // (künftigem) Looper-Playback: Capture-Philosophie "Rohmaterial"
    // (kein Click in der Aufzeichnung) und strukturell feedback-frei
    // (der Looper kann seine eigene Ausgabe nie wieder einfangen).
    {
        const rt::ScopedRealtimeSection rtAudit;
        const auto usable = juce::jmin (getTotalNumOutputChannels(),
                                        buffer.getNumChannels());
        if (usable > 0)
            captureService.writeVirtualChannel (masterTapLeft,
                                                buffer.getReadPointer (0),
                                                buffer.getNumSamples());
        if (usable > 1)
            captureService.writeVirtualChannel (masterTapRight,
                                                buffer.getReadPointer (1),
                                                buffer.getNumSamples());

        // Waveform-Binner der Looper-Page (B4): NACH dem Master-Tap-Write
        // sind alle Quelltypen (Hardware/Modul-Taps/Master) für diesen
        // Block vollständig im Ring
        const auto clockNow = captureService.getSampleClock().now();
        const auto blockSamples = static_cast<std::uint64_t> (buffer.getNumSamples());
        if (clockNow >= blockSamples)
            looperWaveformTap.process (clockBus.current, captureService,
                                       clockNow - blockSamples, buffer.getNumSamples());

        // Loop-Playback (B5): NACH dem Master-Tap (der Looper kann seine
        // eigene Ausgabe nie wieder einfangen) und VOR dem Metronom.
        // Block-Start + Anker speisen den jitter-freien Beat-Playhead.
        looperEngine.process (buffer, getTotalNumOutputChannels(), clockBus.current,
                              clockNow >= blockSamples ? clockNow - blockSamples : 0,
                              barAnchors);
    }

    // Metronom NACH dem Fader (Click faded bei Graph-Swaps nicht mit) und
    // VOR dem Sicht-Metering — der Capture-Tap am Blockanfang bleibt sauber
    {
        const rt::ScopedRealtimeSection rtAudit;
        metronome.process (buffer, getTotalNumOutputChannels(), clockBus.current);
    }

    // Sicht-Metering Out: der Buffer trägt jetzt das finale Signal an die
    // Hardware (nach Fade). Allocation-free wie der Input-Tap.
    {
        const rt::ScopedRealtimeSection rtAudit;
        outputLevels.process (buffer, getTotalNumOutputChannels());
    }

    audioCallbackActive.store (false, std::memory_order_release);
}

bool EngineProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Beide Busse tragen genau eine (Haupt-)Bus-Gruppe; wir nehmen jede
    // Kanalzahl an. Der AudioProcessorPlayer probiert die echte Device-I/O-
    // Zahl vor dem Stereo-Default (findMostSuitableLayout), sodass ein
    // Multichannel-Interface bis in graph.setPlayConfigDetails() durchreicht.
    // Eingänge dürfen 0 sein (Ausgabe-only-Interface, 9.1); Ausgänge sind
    // Conduits Primärzweck, daher mindestens einer.
    return layouts.getMainOutputChannels() >= 1;
}

//==============================================================================
juce::AudioProcessorEditor* EngineProcessor::createEditor() { return new EngineEditor (*this); }
bool EngineProcessor::hasEditor() const                     { return true; }

const juce::String EngineProcessor::getName() const         { return "Conduit Engine"; }
bool EngineProcessor::acceptsMidi() const                   { return true; }
bool EngineProcessor::producesMidi() const                  { return false; }
double EngineProcessor::getTailLengthSeconds() const        { return 0.0; }

int EngineProcessor::getNumPrograms()                       { return 1; }
int EngineProcessor::getCurrentProgram()                    { return 0; }
void EngineProcessor::setCurrentProgram (int)               {}
const juce::String EngineProcessor::getProgramName (int)    { return {}; }
void EngineProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void EngineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Serialisierungs-Guard (6.1): ausstehende OSC-Werte synchron in den
    // Tree flushen, damit beim Speichern nichts verloren geht.
    oscController.flushPendingUpdates();

    // Snapshot des Root-Trees (CLAUDE.md 5.4)
    const auto snapshot = rootState.createCopy();

    if (const auto xml = snapshot.createXml())
        copyXmlToBinary (*xml, destData);
}

void EngineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        const auto loaded = juce::ValueTree::fromXml (*xml);

        if (loaded.hasType (id::root))
        {
            rootState.copyPropertiesAndChildrenFrom (loaded, nullptr);
            ensureIONodeStates();          // Presets ohne I/O-Nodes reparieren
            ensureSessionScaleDefaults();  // ... und ohne Skalen-Properties
        }
    }
}

//==============================================================================
juce::Result EngineProcessor::savePreset (const juce::File& file)
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Serialisierungs-Guard (6.1): kein OSC-Wert geht beim Speichern verloren
    oscController.flushPendingUpdates();

    const auto snapshot = rootState.createCopy();  // CLAUDE.md 5.4

    if (const auto xml = snapshot.createXml())
    {
        if (xml->writeTo (file))
            return juce::Result::ok();

        return juce::Result::fail ("Preset-Datei nicht schreibbar: " + file.getFullPathName());
    }

    return juce::Result::fail ("Preset-Serialisierung fehlgeschlagen");
}

juce::Result EngineProcessor::loadPreset (const juce::File& file)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr)
        return juce::Result::fail ("Kein gültiges Preset-XML: " + file.getFullPathName());

    const auto loaded = juce::ValueTree::fromXml (*xml);

    if (! loaded.hasType (id::root))
        return juce::Result::fail ("Kein Conduit-Preset: " + file.getFullPathName());

    // Undo-fähig in EINER Transaktion — ein Undo stellt den kompletten
    // vorherigen Patch wieder her (Batch-Coalescing 5.5 macht daraus
    // einen einzigen Graph-Swap)
    undoManager.beginNewTransaction ("Preset laden");
    rootState.copyPropertiesAndChildrenFrom (loaded, &undoManager);
    ensureIONodeStates();          // Presets ohne I/O-Nodes reparieren
    ensureSessionScaleDefaults();  // ... und ohne Skalen-Properties

    return juce::Result::ok();
}

//==============================================================================
juce::ValueTree EngineProcessor::getRootState() noexcept       { return rootState; }
juce::UndoManager& EngineProcessor::getUndoManager() noexcept  { return undoManager; }
GraphManager& EngineProcessor::getGraphManager() noexcept      { return graphManager; }
NodeUiRegistry& EngineProcessor::getNodeUiRegistry() noexcept  { return nodeUiRegistry; }
OscController& EngineProcessor::getOscController() noexcept    { return oscController; }
LinkClock& EngineProcessor::getLinkClock() noexcept            { return linkClock; }
const BarSampleAnchors& EngineProcessor::getBarAnchors() const noexcept { return barAnchors; }
const CaptureService& EngineProcessor::getCaptureService() const noexcept { return captureService; }
CaptureService& EngineProcessor::getCaptureService() noexcept   { return captureService; }
CaptureSettings& EngineProcessor::getCaptureSettings() noexcept { return captureSettings; }
ChannelNames& EngineProcessor::getChannelNames() noexcept       { return channelNames; }
LevelMeter& EngineProcessor::getInputLevels() noexcept  { return inputLevels; }
LevelMeter& EngineProcessor::getOutputLevels() noexcept { return outputLevels; }
MeterSettings& EngineProcessor::getMeterSettings() noexcept { return meterSettings; }
InputLinkSend& EngineProcessor::getInputLinkSend() noexcept { return inputLinkSend; }
TransportSettings& EngineProcessor::getTransportSettings() noexcept { return transportSettings; }
OscSendSettings& EngineProcessor::getOscSendSettings() noexcept { return oscSendSettings; }
OscSendService& EngineProcessor::getOscSendService() noexcept   { return oscSendService; }
UiSettings& EngineProcessor::getUiSettings() noexcept           { return uiSettings; }

} // namespace conduit
