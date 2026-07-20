#include "EngineProcessor.h"

#include <algorithm>

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
      looperSettings   (redirectSettings (LooperSettings::defaultOptions(),   settingsFolder)),
      midiRigSettings  (redirectSettings (MidiRigSettings::defaultOptions(),  settingsFolder)),
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

    // Hardware-Anker registrieren (ADR 009): I/O-Nodes sind reguläre
    // Module (AudioEndpointModule), der GraphManager zieht implizite
    // Anker-Kabel zu diesen Prozessoren
    graphManager.registerExternalEndpoint (audioInputModuleId,  audioInputNode->nodeID);
    graphManager.registerExternalEndpoint (audioOutputModuleId, audioOutputNode->nodeID);
    migrateReservedIO();

    // Takt-Verteiler — IClockSlaves bekommen den Bus bei der Materialisierung
    graphManager.setClockBus (&clockBus);

    // Link-Audio-Kontext — ILinkAudioClients (Send-Module) bekommen Clock +
    // moduleId bei der Materialisierung (7.2)
    graphManager.setLinkClock (&linkClock);

    // Capture-Kontext — ICaptureTapClients (Tap-Module) bekommen Service +
    // moduleId bei der Materialisierung (Spurname == moduleId)
    graphManager.setCaptureService (&captureService);

    // Looper-Busse — ILooperAudioClients (Looper patch OUT) lesen die vor dem
    // Graph gerenderten Busse der Bank (Looper-I/O 07/2026)
    graphManager.setLooperBank (&looperBank);

    // Master-Output-Tap (Looper B2): die Session-Summe nach dem GraphFader
    // als zwei virtuelle Capture-Kanäle — Quelle des Retro-Loopers, und
    // Capture All exportiert die Summe automatisch sample-aligned mit.
    // Registrierung VOR jedem Audio-Start, Handles danach unveränderlich —
    // deshalb schlichte Member statt der rtSlot-Atomics des CaptureTapModule.
    masterTapLeft  = captureService.registerVirtualChannel ("master_l");
    masterTapRight = captureService.registerVirtualChannel ("master_r");

    // Slot-Reserve (Looper-I/O 07/2026): Looper-In-Slots und Modul-Taps,
    // die NACH prepare registriert werden, sind bis in die Reserve sofort
    // auflösbar — gearmte Looper-Quellen halten die Kanäle sonst dauerhaft
    // aktiv und blockieren die aufgeschobene Puffersatz-Erweiterung.
    // 12 = Default-Bestückung EINES Looper-In (4× stereo + 4× mono).
    captureService.setVirtualSlotReserve (12);

    // Looper-Quellauflösung folgt der Registry SYNCHRON (Looper-I/O
    // 07/2026): Re-Materialisierung eines Looper-In-Moduls registriert
    // seine Kanäle auf neuen Slots (gearmte alte binden ihr Material als
    // held) — ohne Refresh läse der Looper dauerhaft den toten Index
    captureService.onRegistryChanged = [this] { applyLooperSourceArming(); };

    // Slot-/Modul-Rename ändert den Registry-Namen — gespeicherte
    // tap:-Quell-Keys wandern mit (Auto-Naming der Looper-In-Slots folgt
    // der verkabelten Quelle, 19.07.2026)
    captureService.onChannelRenamed = [this] (const juce::String& oldName,
                                              const juce::String& newName)
    {
        const auto baseOf = [] (const juce::String& channelName)
        {
            return channelName.endsWith ("_l") || channelName.endsWith ("_r")
                     ? channelName.dropLastCharacters (2)
                     : channelName;
        };

        const auto oldKey = "tap:" + baseOf (oldName);
        const auto newKey = "tap:" + baseOf (newName);
        if (oldKey == newKey)
            return;

        for (int l = 0; l < LooperBank::maxLoopers; ++l)
            if (looperSettings.getSourceKey (l) == oldKey)
                looperSettings.setSourceKey (l, newKey);
    };

    // Kanal-Namen — Auto-Naming der Send-Kanäle (7.2): Quelle am audio_input
    // liefert ihr ChannelNames-Label
    graphManager.setChannelNames (&channelNames);

    // Modul-Typ-Defaults des Dev-Modus (4.6) — Overlay bei Neu-Anlagen
    graphManager.setModuleUiDefaults (&moduleUiDefaults);

    // Seiten-Verwaltung (ADR 008 M1): neue Nodes erhalten die pageUuid der
    // aktiven Seite; Migration deckt den frischen Tree gleich mit ab
    graphManager.setPageManager (&pageManager);
    pageManager.migrateAndRepair();

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

    // Looper-Settings (M5): Einmal-Migration der Legacy-Schlüssel
    // (looperSource/looperSpectrum aus den TransportSettings — die alten
    // Keys bleiben dort, werden aber nicht mehr geschrieben), dann in
    // Bank/Modell spiegeln
    looperSettings.migrateFromLegacy (transportSettings.getLooperSource(),
                                      transportSettings.isLooperSpectrumEnabled());
    looperSettings.addChangeListener (this);
    applyLooperSettings();

    // MIDI-Rig (ADR 006 M1b): Einmal-Migration der GridPanel-Gerätenamen
    // in die Registry (Quell-Strings bleiben unangetastet), dann Ports
    // gemäß Registry öffnen — Registry-Änderungen und USB-Reconnects
    // re-synct der Hub danach selbst.
    midiRigSettings.migrateFromGridPanel (gridPanelSettings.getControlMidiInDeviceName(),
                                          gridPanelSettings.getGridMidiOutDeviceName(),
                                          gridPanelSettings.getEchoMidiInDeviceName());
    midiPortHub.syncFromRegistry();

    // Live-Remote-Bridge: Seams an Hub-Ausgang + TouchLive-Client binden
    // (Muster PositionFeedbackRouter -- die Bridge selbst bleibt headless
    // testbar). Danach EINMAL neu aufloesen: der Member-Ctor lief noch ohne
    // sendMidi, das Native-Mode-SysEx eines bereits gesetzten Geraets
    // braucht die fertige Verdrahtung.
    liveRemoteBridge.sendMidi = [this] (const juce::MidiMessage& message)
    {
        midiPortHub.outputTargetFor (midiRigSettings.getLiveRemoteDeviceId()).send (message);
    };
    liveRemoteBridge.sendCommand = [this] (const juce::OSCMessage& message)
    { return touchLiveClient.sendCommand (message); };
    liveRemoteBridge.sendTouchValue = [this] (const juce::OSCMessage& message)
    { touchLiveClient.sendTouchValue (message); };
    liveRemoteBridge.noteTouched = [this] (const juce::String& suppressionKey)
    { touchLiveClient.noteTouchedParameter (suppressionKey); };
    liveRemoteBridge.isLiveConnected = [this]
    { return touchLiveClient.getStatus() == TouchLiveClient::Status::connected; };
    liveRemoteBridge.refreshFromRegistry();

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

    // Looper-Aktionen (M8): Commit/Stop/Target via Push-Pads/Fußschalter —
    // fire-and-forget (Fehler wie „kein Target" werden still verworfen,
    // der Sender bekommt kein Feedback; Stop nutzt die Launch-Quant der
    // Settings, Commit ist per Konstruktion sofort)
    oscController.onLooperAction = [this] (const osc::LooperOscAction& action)
    {
        using Type = osc::LooperOscAction::Type;
        const auto qBeats = launchQuantBeats (looperSettings.getLaunchQuant());

        switch (action.type)
        {
            case Type::commit:
                juce::ignoreUnused (commitToTarget (action.looperIndex, action.bars));
                break;

            case Type::stopTrack:
                looperSession.stopTrack (action.looperIndex, action.trackIndex, qBeats);
                break;

            case Type::stopLooper:
                for (int t = 0; t < LooperBank::maxTracks; ++t)
                    looperSession.stopTrack (action.looperIndex, t, qBeats);
                break;

            case Type::stopAll:
                for (int l = 0; l < looperSession.getNumLoopers(); ++l)
                    for (int t = 0; t < LooperBank::maxTracks; ++t)
                        looperSession.stopTrack (l, t, qBeats);
                break;

            case Type::target:
                looperSession.armTarget (action.looperIndex, action.trackIndex,
                                         action.slotIndex);
                break;

            case Type::none:
                break;
        }
    };
}

EngineProcessor::~EngineProcessor()
{
    // Modul-Destruktoren deregistrieren ihre Capture-Kanäle noch — die
    // Registry-Hooks dürfen dabei nicht mehr feuern
    captureService.onRegistryChanged = nullptr;
    captureService.onChannelRenamed = nullptr;

    channelNames.removeChangeListener (this);
    meterSettings.removeChangeListener (this);
    transportSettings.removeChangeListener (this);
    looperSettings.removeChangeListener (this);
    rootState.removeListener (this);
}

//==============================================================================
void EngineProcessor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &meterSettings)
        applyMeterSettings();
    else if (source == &transportSettings)
        applyTransportSettings();
    else if (source == &looperSettings)
        applyLooperSettings();
    else if (source == &channelNames)
        rebuildInputSends();
}

void EngineProcessor::applyLooperSettings()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Struktur-Sync (idempotent, best effort): Modell folgt den Settings.
    // Beim Start sind keine Clips da — remove kann nicht scheitern;
    // zur Laufzeit hält das UI beide ohnehin synchron (M6).
    while (looperSession.getNumLoopers() < looperSettings.getNumLoopers())
        if (! looperSession.addLooper())
            break;
    while (looperSession.getNumLoopers() > looperSettings.getNumLoopers())
        if (looperSession.removeLastLooper().failed())
            break;

    for (int l = 0; l < looperSession.getNumLoopers(); ++l)
    {
        while (looperSession.getNumTracks (l) < looperSettings.getNumTracks (l))
            if (! looperSession.addTrack (l))
                break;
        while (looperSession.getNumTracks (l) > looperSettings.getNumTracks (l))
            if (looperSession.removeLastTrack (l).failed())
                break;
    }

    looperSession.setAutoAdvance (looperSettings.isAutoAdvanceEnabled());
    looperBank.setSoloScopeGlobal (looperSettings.getSoloScope()
                                   == LooperSettings::SoloScope::globalScope);

    for (int l = 0; l < LooperBank::maxLoopers; ++l)
    {
        // „an Master senden" pro Looper (Looper-I/O 07/2026)
        looperBank.setLooperToMaster (l, looperSettings.isSendToMaster (l));

        for (int t = 0; t < LooperBank::maxTracks; ++t)
        {
            looperBank.setTrackGain (l, t, looperSettings.getTrackGain (l, t));
            looperBank.setTrackPan  (l, t, looperSettings.getTrackPan (l, t));
            looperBank.setTrackMute (l, t, looperSettings.isTrackMuted (l, t));
            looperBank.setTrackSolo (l, t, looperSettings.isTrackSolo (l, t));
            looperBank.setTrackSends (l, t,
                (std::uint32_t) looperSettings.getTrackSends (l, t));
            looperBank.setTrackSendPre (l, t, looperSettings.isTrackSendPre (l, t));
        }
    }

    // Big-Out-Nodes folgen der Struktur (Auto-Follow, gefadete Re-Mat)
    graphManager.setLooperStructure (currentLooperStructure());

    applyLooperSourceArming();
}

LooperPatchOutModule::Structure EngineProcessor::currentLooperStructure() const
{
    LooperPatchOutModule::Structure structure;
    structure.numLoopers = looperSettings.getNumLoopers();
    for (int l = 0; l < LooperBank::maxLoopers; ++l)
        structure.numTracks[(size_t) l] = looperSettings.getNumTracks (l);
    return structure;
}

//==============================================================================
juce::Result EngineProcessor::forceRemoveLooperTrack (int looperIndex)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (looperIndex < 0 || looperIndex >= looperSession.getNumLoopers())
        return juce::Result::fail ("Ungültiger Looper");

    const auto trackIndex = looperSession.getNumTracks (looperIndex) - 1;
    if (trackIndex < 1)
        return juce::Result::fail ("Der letzte Track bleibt");

    LooperTrashCan::Entry entry;
    entry.kind = LooperTrashCan::Entry::Kind::track;
    entry.looperIndex = looperIndex;
    entry.trackIndex = trackIndex;

    // (1) Stoppen (MT-Intent sofort; der Voice-Fade läuft audio-seitig
    // weiter — die Clips bleiben am Leben)
    looperSession.stopTrack (looperIndex, trackIndex, 0.0);

    // (2) Kabel VOR dem Struktur-Sync einsammeln (Spec-Liste noch gültig)
    entry.cables = graphManager.collectAndRemovePatchOutCables (looperIndex, trackIndex);

    // (3) Clips in den Papierkorb detachen — die Bank bleibt Besitzerin
    for (int slot = 0; slot < LooperSessionModel::maxSlots; ++slot)
        if (auto* clip = looperSession.detachSlot (looperIndex, trackIndex, slot))
            entry.clips.push_back ({ trackIndex, slot, clip, clip->clipId });

    // (4) Struktur schrumpfen (passt jetzt — Track ist leer und gestoppt)
    if (const auto result = looperSession.removeLastTrack (looperIndex); result.failed())
        return result;
    looperSettings.setNumTracks (looperIndex, looperSession.getNumTracks (looperIndex));

    // (5) Big-Out-Nodes SYNCHRON nachziehen (keine Async-Reihenfolge-Falle)
    graphManager.setLooperStructure (currentLooperStructure());

    looperTrash.push (std::move (entry));
    return juce::Result::ok();
}

juce::Result EngineProcessor::forceRemoveLastLooper()
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto looperIndex = looperSession.getNumLoopers() - 1;
    if (looperIndex < 1)
        return juce::Result::fail ("Der letzte Looper bleibt offen");

    LooperTrashCan::Entry entry;
    entry.kind = LooperTrashCan::Entry::Kind::looper;
    entry.looperIndex = looperIndex;
    // VOR removeLastLooper snappen — das resettet numTracks auf 1
    entry.numTracksSnapshot = looperSession.getNumTracks (looperIndex);

    entry.cables = graphManager.collectAndRemovePatchOutCables (looperIndex, -1);

    for (int t = 0; t < entry.numTracksSnapshot; ++t)
    {
        looperSession.stopTrack (looperIndex, t, 0.0);
        for (int slot = 0; slot < LooperSessionModel::maxSlots; ++slot)
            if (auto* clip = looperSession.detachSlot (looperIndex, t, slot))
                entry.clips.push_back ({ t, slot, clip, clip->clipId });
    }

    if (const auto result = looperSession.removeLastLooper(); result.failed())
        return result;
    looperSettings.setNumLoopers (looperSession.getNumLoopers());
    looperSettings.setNumTracks (looperIndex, 1);   // Settings-Reset wie im Modell

    graphManager.setLooperStructure (currentLooperStructure());

    looperTrash.push (std::move (entry));
    return juce::Result::ok();
}

juce::Result EngineProcessor::trashClipSlot (int looperIndex, int trackIndex,
                                             int slotIndex)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto* clip = looperSession.clipAt (looperIndex, trackIndex, slotIndex);
    if (clip == nullptr)
        return juce::Result::fail ("Slot ist leer");

    // Spielender Clip: sofort stoppen (Voice-Fade läuft audio-seitig
    // weiter, der Clip bleibt am Leben — Muster forceRemoveLooperTrack)
    if (looperSession.getPlayingSlot (looperIndex, trackIndex) == slotIndex)
        looperSession.stopTrack (looperIndex, trackIndex, 0.0);

    LooperTrashCan::Entry entry;
    entry.kind = LooperTrashCan::Entry::Kind::clip;
    entry.looperIndex = looperIndex;
    entry.trackIndex = trackIndex;

    if (auto* detached = looperSession.detachSlot (looperIndex, trackIndex, slotIndex))
        entry.clips.push_back ({ trackIndex, slotIndex, detached, detached->clipId });
    else
        return juce::Result::fail ("Slot ist leer");

    looperTrash.push (std::move (entry));
    return juce::Result::ok();
}

juce::Result EngineProcessor::restoreLooperTrash (int* skippedCables)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (! looperTrash.hasEntries())
    {
        if (skippedCables != nullptr)
            *skippedCables = 0;
        return juce::Result::fail ("Papierkorb ist leer");
    }

    return restoreTrashEntry (looperTrash.popLatest(), skippedCables);
}

juce::Result EngineProcessor::restoreLooperTrashEntry (std::uint32_t entryId,
                                                       int* skippedCables)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto entry = looperTrash.popEntry (entryId);
    if (entry.entryId == 0)
    {
        if (skippedCables != nullptr)
            *skippedCables = 0;
        return juce::Result::fail ("Eintrag ist inzwischen abgelaufen");
    }

    return restoreTrashEntry (std::move (entry), skippedCables);
}

juce::Result EngineProcessor::restoreTrashEntry (LooperTrashCan::Entry entry,
                                                 int* skippedCables)
{
    if (skippedCables != nullptr)
        *skippedCables = 0;

    // Einzel-Clip: Looper/Track müssen noch existieren, der Slot frei
    // sein — sonst Eintrag zurücklegen (kein Struktur-/Kabel-Umbau)
    if (entry.kind == LooperTrashCan::Entry::Kind::clip)
    {
        jassert (entry.clips.size() == 1);

        if (entry.looperIndex >= looperSession.getNumLoopers()
            || entry.trackIndex >= looperSession.getNumTracks (entry.looperIndex))
        {
            looperTrash.push (std::move (entry));
            return juce::Result::fail ("Track existiert nicht mehr");
        }

        for (const auto& ref : entry.clips)
        {
            if (! looperSession.attachClip (entry.looperIndex, ref.track,
                                            ref.slot, ref.clip))
            {
                looperTrash.push (std::move (entry));
                return juce::Result::fail ("Slot inzwischen belegt");
            }
        }

        return juce::Result::ok();
    }

    // Struktur-Position muss frei sein — sonst Eintrag zurücklegen
    // (push erneuert das Zeitfenster, bewusst großzügig)
    if (entry.kind == LooperTrashCan::Entry::Kind::track)
    {
        if (looperSession.getNumTracks (entry.looperIndex) != entry.trackIndex)
        {
            looperTrash.push (std::move (entry));
            return juce::Result::fail ("Struktur inzwischen belegt");
        }

        looperSession.addTrack (entry.looperIndex);
        looperSettings.setNumTracks (entry.looperIndex,
                                     looperSession.getNumTracks (entry.looperIndex));
    }
    else
    {
        if (looperSession.getNumLoopers() != entry.looperIndex)
        {
            looperTrash.push (std::move (entry));
            return juce::Result::fail ("Struktur inzwischen belegt");
        }

        looperSession.addLooper();
        while (looperSession.getNumTracks (entry.looperIndex) < entry.numTracksSnapshot)
            if (! looperSession.addTrack (entry.looperIndex))
                break;

        looperSettings.setNumLoopers (looperSession.getNumLoopers());
        looperSettings.setNumTracks (entry.looperIndex, entry.numTracksSnapshot);
    }

    // Clips reattachen (kein Auto-Play) — frische Tracks sind leer
    for (const auto& ref : entry.clips)
        looperSession.attachClip (entry.looperIndex, ref.track, ref.slot, ref.clip);

    // Slots nachwachsen lassen, DANN Kabel spec-relativ neu anlegen
    graphManager.setLooperStructure (currentLooperStructure());
    const auto failed = graphManager.restorePatchOutCables (entry.cables);
    if (skippedCables != nullptr)
        *skippedCables = failed;

    return juce::Result::ok();
}

void EngineProcessor::applyTransportSettings()
{
    linkClock.setStartStopSyncEnabled (transportSettings.isStartStopSyncEnabled());
    linkClock.setClockOffsetMs (transportSettings.getClockOffsetMs());
    metronome.setEnabled (transportSettings.isMetronomeEnabled());
    metronome.setAnchor (transportSettings.getMetronomeAnchor());
    looperBank.setAnchor (transportSettings.getLooperAnchor());
}

juce::Result EngineProcessor::commitLooper (int bars)
{
    // Paritäts-Pfad des alten UI: ein Loop auf Looper 0 / Track 0
    return looperBank.commitAndPlay (0, 0, bars, captureService,
                                     looperLeftIndex[0], looperRightIndex[0],
                                     barAnchors);
}

juce::Result EngineProcessor::commitToTarget (int looperIndex, int bars)
{
    if (looperIndex < 0 || looperIndex >= LooperBank::maxLoopers)
        return juce::Result::fail ("Ungültiger Looper");

    return looperSession.commit (looperIndex, bars, captureService,
                                 looperLeftIndex[static_cast<std::size_t> (looperIndex)],
                                 looperRightIndex[static_cast<std::size_t> (looperIndex)],
                                 barAnchors);
}

void EngineProcessor::setLooperAnchor (int pairIndex)
{
    transportSettings.setLooperAnchor (pairIndex);
    looperBank.setAnchor (transportSettings.getLooperAnchor());
}

//==============================================================================
void EngineProcessor::setLooperSource (int looperIndex, const juce::String& sourceKey)
{
    if (looperIndex < 0 || looperIndex >= LooperBank::maxLoopers)
        return;

    // Persistenz in den LooperSettings (M5) — der Broadcast läuft async,
    // das Arming folgt hier sofort (Muster setLooperAnchor)
    looperSettings.setSourceKey (looperIndex, sourceKey);
    applyLooperSourceArming();
}

namespace
{
    /** Quell-Schlüssel ("master" | "hw:{paar}" | "hwm:{kanal}" | "tap:{name}")
        in Capture-Indizes auflösen (B3-Logik, nur herausgelöst).

        Mono-Quellen (hwm:, Mono-Taps) lassen right = -1 — der Commit erzeugt
        dann 1-Kanal-Clips (Looper-I/O 07/2026). Der frühere "out:{paar}"-
        Zweig entfiel mit den Ausgangs-Paar-Taps: Ausgangs-Signale loopt man
        jetzt per Kabel ins Looper-In-Modul (tap:-Pfad). */
    void resolveLooperSourceKey (const conduit::CaptureService& capture,
                                 const juce::String& key, int& left, int& right)
    {
        left = -1;
        right = -1;

        if (key == "master")
        {
            // Master-Output-Tap (B2): Registry-Slots 0/1; captureIndex ist −1,
            // solange kein Puffersatz die Slots trägt (vor prepare)
            left  = capture.getVirtualChannelUiInfo (0).captureIndex;
            right = capture.getVirtualChannelUiInfo (1).captureIndex;
        }
        else if (key.startsWith ("hw:"))
        {
            // Hardware-Eingangs-Paar 2n/2n+1 — nur Kanäle, die der aktuelle
            // Puffersatz tatsächlich trägt
            const auto pair = key.substring (3).getIntValue();
            const auto channels = capture.getRingNumChannels();
            const auto leftChannel = pair * 2;

            if (leftChannel < channels)
                left = leftChannel;
            if (leftChannel + 1 < channels)
                right = leftChannel + 1;
        }
        else if (key.startsWith ("hwm:"))
        {
            // Einzelner Hardware-Kanal (Mono-Eintrag — ungepaarte Kanäle
            // nach ∥-Pairing der ChannelNames)
            const auto channel = key.substring (4).getIntValue();

            if (channel >= 0 && channel < capture.getRingNumChannels())
                left = channel;
        }
        else if (key.startsWith ("hws:"))
        {
            // Stereo-Paar mit beliebigem Anker-Kanal (∥-Pairing kann auch
            // ungerade Kanäle koppeln — "hw:{paar}" deckt nur 2n/2n+1)
            const auto channel = key.substring (4).getIntValue();
            const auto channels = capture.getRingNumChannels();

            if (channel >= 0 && channel < channels)
                left = channel;
            if (channel >= 0 && channel + 1 < channels)
                right = channel + 1;
        }
        else if (key.startsWith ("tap:"))
        {
            // Capture-Tap eines Moduls: Basisname → _l/_r-Paar (Stereo) bzw.
            // exakter Name (Mono-Tap)
            const auto baseName = key.substring (4);

            for (int slot = 0; slot < conduit::CaptureService::MAX_VIRTUAL_CHANNELS; ++slot)
            {
                const auto info = capture.getVirtualChannelUiInfo (slot);
                if (! info.inUse || info.captureIndex < 0)
                    continue;

                if (info.name == baseName + "_l" || info.name == baseName)
                    left = info.captureIndex;
                else if (info.name == baseName + "_r")
                    right = info.captureIndex;
            }
        }
    }
} // namespace

void EngineProcessor::applyLooperSourceArming()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Alle Looper-Quellen auflösen; das Arming folgt der VEREINIGUNG —
    // Diff gegen den Vorzustand, damit eine GETEILTE Quelle offen bleibt,
    // wenn nur EINER von zwei Loopern sie verlässt (Refcount-Semantik).
    std::vector<int> nowArmed;

    for (int l = 0; l < LooperBank::maxLoopers; ++l)
    {
        const auto key = looperSettings.getSourceKey (l);

        auto& left  = looperLeftIndex[static_cast<std::size_t> (l)];
        auto& right = looperRightIndex[static_cast<std::size_t> (l)];

        if (key.isEmpty())
        {
            left = -1;
            right = -1;
        }
        else
        {
            resolveLooperSourceKey (captureService, key, left, right);
        }

        for (const auto index : { left, right })
            if (index >= 0 && std::find (nowArmed.begin(), nowArmed.end(), index)
                                  == nowArmed.end())
                nowArmed.push_back (index);

        // Waveform-Binner folgt seiner Quelle (Reset + Backfill im Audio
        // Thread, B4) — ein Tap pro Looper. Mono-Quelle (right < 0): die
        // Anzeige liest links auf beiden Seiten, der Commit bleibt mono.
        looperWaveformTaps[static_cast<std::size_t> (l)].setSource (
            left, right >= 0 ? right : left);
    }

    // Diff: Verlassene entwaffnen (die normale Gate-Detektion übernimmt,
    // B1), Neue armen (setChannelArmed ist idempotent)
    for (const auto index : looperArmedIndices)
        if (std::find (nowArmed.begin(), nowArmed.end(), index) == nowArmed.end())
            captureService.setChannelArmed (index, false);

    for (const auto index : nowArmed)
        captureService.setChannelArmed (index, true);

    looperArmedIndices = std::move (nowArmed);
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
    looperOutLevels.setClipHoldSeconds (hold);

    // Ballistik gilt app-weit fuer ALLE LevelMeter-Instanzen (Input/Output,
    // FX-Chassis, Looper) — klassenweite Atomics (User-Feintuning 14.07.2026).
    LevelMeter::setGlobalBallistics (meterSettings.getRmsWindowSeconds(),
                                     meterSettings.getPeakReleaseSeconds(),
                                     meterSettings.getPeakHoldSeconds());
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
void EngineProcessor::migrateReservedIO()
{
    // ADR 009: ab Version 3 sind I/O-Nodes reguläre Module — gelöschte
    // bleiben gelöscht (Patch ohne Output = bewusste Stille), KEIN Repair.
    if ((int) rootState.getProperty (id::rootStateVersion, 1) >= ioRootVersion)
        return;

    auto nodesTree = rootState.getChildWithName (id::nodes);

    const auto ensure = [&nodesTree] (const char* factoryKey, const char* defaultName,
                                      int numInputs, int numOutputs, int x, int y)
    {
        // Vorhanden? factoryId-Match; Alt-Bestände tragen den Schlüssel in
        // moduleId — deren Subtrees sind strukturgleich und bleiben (die
        // Wandlung Reserved → regulär ist rein verhaltensseitig)
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

        nodesTree.appendChild (node, nullptr);  // Migration — kein Undo
    };

    // Aus Graph-Sicht: der Input-Prozessor LIEFERT Kanäle (Outputs),
    // der Output-Prozessor NIMMT Kanäle entgegen (Inputs)
    ensure (audioInputModuleId,  "audio_in",  0, 2, 40,  260);
    ensure (audioOutputModuleId, "audio_out", 2, 0, 700, 260);

    rootState.setProperty (id::rootStateVersion, ioRootVersion, nullptr);
}

void EngineProcessor::syncHardwareIOChannels (int deviceInputs, int deviceOutputs)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto ins  = juce::jmax (0, deviceInputs);
    const auto outs = juce::jmax (0, deviceOutputs);

    // Für neue Browser-Instanzen merken (valueTreeChildAdded, ADR 009)
    lastDeviceInputs  = ins;
    lastDeviceOutputs = outs;

    auto nodesTree = rootState.getChildWithName (id::nodes);

    // ALLE I/O-Endpunkt-Instanzen (ADR 009: Mehrfach-Instanzen) — Kanalzahl
    // idempotent setzen (nur bei Abweichung, sonst unnötige UI-Rebuilds).
    // Input-Prozessor LIEFERT Kanäle → Ausgangs-Ports am audio_in-Node;
    // Output-Prozessor NIMMT Kanäle → Eingangs-Ports am audio_out-Node.
    // Geräte-getrieben → kein Undo. Schritt C: Kabel auf verschwundene
    // Kanäle kappen, damit keine Phantom-Connections zurückbleiben.
    for (int i = 0; i < nodesTree.getNumChildren(); ++i)
    {
        auto node = nodesTree.getChild (i);
        const auto factoryKey = GraphManager::factoryKeyOf (node);

        if (factoryKey == audioInputModuleId)
        {
            if ((int) node.getProperty (id::numOutputChannels, -1) != ins)
                node.setProperty (id::numOutputChannels, ins, nullptr);

            pruneEndpointConnections (node.getProperty (id::nodeId).toString(), true, ins);
        }
        else if (factoryKey == audioOutputModuleId)
        {
            if ((int) node.getProperty (id::numInputChannels, -1) != outs)
                node.setProperty (id::numInputChannels, outs, nullptr);

            pruneEndpointConnections (node.getProperty (id::nodeId).toString(), false, outs);
        }
    }

    // Input-Link-Sends an die neue Kanalzahl anpassen (Schrumpfen retired
    // die betroffenen Kanäle; buildSpecs sieht nur noch gültige Anker)
    rebuildInputSends();
}

void EngineProcessor::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    // Neue I/O-Endpunkt-Instanz aus dem Browser (ADR 009): sofort auf die
    // Hardware-Kanalzahl bringen (auch nach Preset-Load — die Hardware ist
    // für Kanalzahlen die Wahrheit). Kein Undo (Umgebungs-Zustand).
    if (! parent.hasType (id::nodes) || ! child.hasType (id::node)
        || lastDeviceInputs < 0)
        return;

    const auto factoryKey = GraphManager::factoryKeyOf (child);

    if (factoryKey == audioInputModuleId
        && (int) child.getProperty (id::numOutputChannels, -1) != lastDeviceInputs)
        child.setProperty (id::numOutputChannels, lastDeviceInputs, nullptr);
    else if (factoryKey == audioOutputModuleId
             && (int) child.getProperty (id::numInputChannels, -1) != lastDeviceOutputs)
        child.setProperty (id::numInputChannels, lastDeviceOutputs, nullptr);
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
    liveSpectrumTap.setAudioSampleRate (sampleRate);
    captureService.prepare (sampleRate, samplesPerBlock, getTotalNumInputChannels());
    barAnchors.reset();  // SampleClock-Reset invalidiert alle Anker-Positionen

    // Looper-Quelle neu auflösen: der frische Puffersatz vergibt die
    // Capture-Indizes der virtuellen Slots neu (B3); Waveform-Binner und
    // Loop-Playback verwerfen ihren Stand (SampleClock-Reset, B4/B5)
    for (auto& tap : looperWaveformTaps)
        tap.prepare (sampleRate);
    looperTrash.clearWithoutDelete();   // VOR bank.prepare — der Store wird
                                        // gleich freigegeben, die Papierkorb-
                                        // Pointer wären sonst dangling
    looperBank.prepare (sampleRate, samplesPerBlock);
    looperSession.clearAllClips();   // Bank hat die Clips freigegeben (M6-Feld-Fund)
    applyLooperSourceArming();
    inputLevels.prepare  (sampleRate, getTotalNumInputChannels());
    outputLevels.prepare (sampleRate, getTotalNumOutputChannels());
    looperOutLevels.prepare (sampleRate, LooperPatchOutModule::meterChannelCount);
    inputLinkSend.prepare (samplesPerBlock);
    timingMonitor.prepare (sampleRate);  // frische XRun-/Load-Diagnose
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

    // Timing-Diagnose (Dev-Modus): Gap-/Load-Messung umschließt den
    // gesamten Callback — beginBlock vor allem anderen
    timingMonitor.beginBlock();

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

        // Spektrum-Zubringer der EQ-Anzeige (§10k, atomic-gated, RT-safe)
        liveSpectrumTap.pushAudioBlock (buffer.getArrayOfReadPointers(),
                                        juce::jmin (getTotalNumInputChannels(),
                                                    buffer.getNumChannels()),
                                        buffer.getNumSamples());
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

    // Looper-Rendering VOR dem Graph (Looper-I/O 07/2026): das Playback
    // liest nur committete Clips und braucht den Graph-Block nicht — das
    // Looper-patch-OUT-Modul liest die Busse damit sample-aligned im SELBEN
    // Callback (keine Block-Latenz). Der Master-Mix wird erst NACH dem
    // Graph additiv ausgegeben (mixToOutput unten) — die Feedback-Freiheit
    // des Master-Taps bleibt: der Tap sieht das Looper-Signal nie.
    {
        const rt::ScopedRealtimeSection rtAudit;
        const auto clockNow = captureService.getSampleClock().now();
        const auto blockSamples = static_cast<std::uint64_t> (buffer.getNumSamples());
        looperBank.renderBlock (clockBus.current,
                                clockNow >= blockSamples ? clockNow - blockSamples : 0,
                                barAnchors, buffer.getNumSamples());

        // Sicht-Metering der Looper-Busse (Looper-patch-OUT-Zeilen):
        // stabiles 4er-Raster-Layout (meterChannelOf) aus der AudioView —
        // Busse sind pro Block genullt, inaktive Slots zeigen Stille
        if (const auto view = looperBank.getAudioView(); view.numSamples > 0)
        {
            const float* channels[LooperPatchOutModule::meterChannelCount] = {};
            for (int l = 0; l < LooperBank::maxLoopers; ++l)
            {
                for (int t = 0; t < LooperBank::maxTracks; ++t)
                    for (int c = 0; c < 2; ++c)
                        channels[(l * 4 + t) * 2 + c] = view.track[l][t][c];
                for (int c = 0; c < 2; ++c)
                    channels[(16 + l) * 2 + c] = view.post[l][c];
            }
            for (int s = 0; s < LooperBank::maxSends; ++s)
                for (int c = 0; c < 2; ++c)
                    channels[(20 + s) * 2 + c] = view.send[s][c];
            channels[48] = view.master[0];
            channels[49] = view.master[1];

            looperOutLevels.processPointers (channels,
                                             LooperPatchOutModule::meterChannelCount,
                                             view.numSamples);
        }
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
            for (auto& tap : looperWaveformTaps)
                tap.process (clockBus.current, captureService,
                             clockNow - blockSamples, buffer.getNumSamples());

        // Loop-Playback-Ausgabe (B5): Master-Mix des VOR dem Graph
        // gerenderten Blocks — NACH dem Master-Tap (der Looper kann seine
        // eigene Ausgabe nie wieder einfangen) und VOR dem Metronom.
        // Anker −1 = „Kein Master-Out" (mixToOutput schreibt nichts).
        looperBank.mixToOutput (buffer, getTotalNumOutputChannels());
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

    timingMonitor.endBlock (buffer.getNumSamples());
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
            graphManager.normalizeLoadedNodes();  // factoryId-Aliase (ADR 013) VOR den Syncs
            migrateReservedIO();           // ADR 009: nur Alt-Patches (< V3)
            ensureSessionScaleDefaults();  // Presets ohne Skalen-Properties
            pageManager.migrateAndRepair();  // ... und ohne Pages-Zweig (ADR 008 M1)
            graphManager.syncLooperPatchOutConfigs();  // Looper patch OUT folgt der App-Struktur
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
    graphManager.normalizeLoadedNodes();  // factoryId-Aliase (ADR 013), undo-frei
    migrateReservedIO();           // ADR 009: nur Alt-Patches (< V3), undo-frei
    ensureSessionScaleDefaults();  // Presets ohne Skalen-Properties
    pageManager.migrateAndRepair();  // ... und ohne Pages-Zweig (ADR 008 M1, undo-frei)

    // Big Out folgt der App-Struktur, nicht dem Preset (Reconcile fremd-
    // strukturierter Patches, undo-frei)
    graphManager.syncLooperPatchOutConfigs();

    return juce::Result::ok();
}

//==============================================================================
juce::ValueTree EngineProcessor::getRootState() noexcept       { return rootState; }
juce::UndoManager& EngineProcessor::getUndoManager() noexcept  { return undoManager; }
GraphManager& EngineProcessor::getGraphManager() noexcept      { return graphManager; }
NodeUiRegistry& EngineProcessor::getNodeUiRegistry() noexcept  { return nodeUiRegistry; }
PageManager& EngineProcessor::getPageManager() noexcept        { return pageManager; }
ModuleFactory& EngineProcessor::getModuleFactory() noexcept    { return moduleFactory; }
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
