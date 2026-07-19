#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "GraphFader.h"
#include "Interfaces/IClockSource.h"
#include "Modules/ConduitModule.h"
#include "Modules/LooperBigOutModule.h"
#include "ParamModulation.h"

namespace conduit
{

class CaptureService;
class ChannelNames;
class LinkClock;
class LooperBank;
class ModuleFactory;
class IExternalAudioEndpoint;
class ModuleUiDefaults;
class NodeUiRegistry;
class PageManager;
class ProcessorModule;

//==============================================================================
/**
    Koordiniert Topologie-Änderungen des AudioProcessorGraph (CLAUDE.md 5).

    Batch-Coalescing (5.5): alle topologie-relevanten ValueTree-Änderungen
    eines Frames werden via AsyncUpdater gesammelt und zu einem einzigen
    Graph-Swap zusammengefasst — gilt für Undo, Redo, Preset-Load,
    Bulk-Delete und Copy-Paste.

    Ablauf pro Swap (5.2):
      Schritt 1: Async Prepare — neue Module via ModuleFactory instanziieren
                 und prepareForGraph() VOR dem Fade-Out abschließen;
                 Fehler → nodeError im ValueTree, kein Retry-Loop
      Schritt 2: fader.beginFadeOut() — Topologie bleibt unverändert
      Schritt 3: Self-Re-Dispatch via triggerAsyncUpdate() bis der Audio
                 Thread Stille meldet (kein Busy-Poll, kein Timer), dann
                 Tree → Graph synchronisieren: verschwundene Nodes entfernen,
                 vorbereitete Module einfügen, Connections abgleichen
      Schritt 4: fader.beginFadeIn()
    Änderungen, die während des Fade-Outs eintreffen, landen im selben Swap.
    Läuft Audio nicht (Fader unprepared), wird ohne Fade direkt geswappt.

    Zweiphasiges Delete (5.3):
      Phase 1: requestNodeDelete() setzt nodeState → Deleting. UI-Components
               reagieren über ihren Listener, entkoppeln sich und geben ihre
               NodeUiRegistry-Referenz frei; moduleId wird sofort gecacht
               (der künftige OscController deregistriert darüber, 7.1)
      Phase 2: im nächsten Loop-Durchlauf, erst wenn der UI-Refcount 0 ist —
               Subtree + zugehörige Kabel werden in einer UndoManager-
               Transaktion entfernt; die Graph-Entfernung folgt über den
               normalen Fade-Swap

    Die I/O-Nodes des EngineProcessor sind nicht tree-verwaltet und bleiben
    von der Synchronisation unangetastet.

    Parameter-Wert-Änderungen (OSC-Dual-State, 6.1) lösen bewusst KEINEN
    Rebuild aus — nur Child-Add/Remove unter Nodes[]/Connections[] sowie
    der Austausch ganzer Container (Preset-Load) markieren das Delta.

    Läuft vollständig auf dem Message Thread — daher ist topologyDirty
    bewusst kein std::atomic. Kein UI-Code, kein DSP-Code (der Fade selbst
    lebt im GraphFader auf dem Audio Thread).
*/
class GraphManager final : public IParamModulationSink,
                           private juce::ValueTree::Listener,
                           private juce::AsyncUpdater
{
public:
    /** rootTree ist ein ref-counted ValueTree-Handle, processorGraph das
        Swap-Ziel, faderToUse der Master-Fade im Audio-Pfad, factoryToUse
        erzeugt Module aus persistierten moduleIds, undoManagerToUse macht
        Deletes undo-fähig, uiRegistryToUse liefert den Zombie-UI-Schutz.
        Registriert sich als Listener. */
    GraphManager (juce::ValueTree rootTree,
                  juce::AudioProcessorGraph& processorGraph,
                  GraphFader& faderToUse,
                  ModuleFactory& factoryToUse,
                  juce::UndoManager& undoManagerToUse,
                  NodeUiRegistry& uiRegistryToUse);

    /** Deregistriert den Listener und verwirft ausstehende Async-Updates. */
    ~GraphManager() override;

    //==========================================================================
    /** Patch-Aktion: erzeugt das Modul über die Factory, baut seinen State
        (createState VOR addNode, 4.4) und hängt ihn in einer eigenen
        UndoManager-Transaktion an Nodes[] — die Materialisierung folgt über
        den normalen Swap. Invalides ValueTree bei unbekannter moduleId.

        configure (optional) läuft nach createState() und vor dem Einhängen —
        für Module mit Anlege-Konfiguration (z.B. Multi-Input Link Audio Send:
        Eingangszahl/Modi). Message Thread. */
    juce::ValueTree addModuleNode (const juce::String& factoryKey, juce::Point<int> position,
                                   const std::function<void (juce::ValueTree&)>& configure = {});

    /** Patch-Aktion: benennt die named_id (OSC-Pfad, UI-Titel) eines Nodes
        undo-fähig um. Der Name wird OSC-pfadtauglich saniert (lowercase,
        [a-z0-9_]); false bei leerem Ergebnis, vergebenem Namen oder
        unbekanntem Node. Message Thread. */
    bool renameNode (const juce::String& nodeUuid, const juce::String& requestedName);

    /** Patch-Aktion (M-B): Node-Farbe (0x00RRGGBB, 0 = keine) — Quellfarbe der
        ausgehenden Kabel. Undo-fähig, patch-persistent; NICHT tintColour (die
        gehört dem M4L-Announce). false bei unbekanntem Node. Message Thread. */
    bool setNodeColour (const juce::String& nodeUuid, juce::uint32 colour);

    /** Patch-Aktion (FX-Chassis 4.6): Link-Send-Tap eines Processor-Nodes
        an/aus — undo-fähig; der Property-Listener leitet live ans Modul
        weiter (create/retire ohne Rebuild). false bei unbekanntem Node.
        Message Thread. */
    bool setLinkSendEnabled (const juce::String& nodeUuid, bool enabled);

    //==========================================================================
    // Patch-Aktionen Looper-I/O (ADR 010) — alle undo-fähig in EINER
    // Transaktion; Kanalzahl-Änderungen re-materialisieren den Node im
    // nächsten gefadeten Swap. Message Thread.

    /** Looper-In-Slot anhängen (stereo = 2 Kanäle). false bei unbekanntem
        oder fremdem Node. */
    bool addLooperInSlot (const juce::String& nodeUuid, bool stereo);

    /** Looper-In-Slot entfernen; Kabel auf den Slot-Kanälen werden mit
        entfernt, dahinterliegende Kanäle rücken nach (Kabel-Remap). Der
        letzte Slot bleibt stehen. */
    bool removeLooperInSlot (const juce::String& nodeUuid, int slotIndex);

    /** Looper-Out-Abgriff anhängen: target 0 = Master, 1..4 = Looper n;
        mode "stereo"|"sum"|"left"|"right"; pre = Pre-Fader. */
    bool addLooperOutSlot (const juce::String& nodeUuid, int target,
                           const juce::String& mode, bool pre);

    /** Looper-Out-Abgriff entfernen (Kabel-Remap wie beim Looper-In). */
    bool removeLooperOutSlot (const juce::String& nodeUuid, int slotIndex);

    /** Pre/Post eines Looper-Out-Abgriffs — re-materialisiert (das Modul
        liest die Slots nur bei der Materialisierung). */
    bool setLooperOutSlotPre (const juce::String& nodeUuid, int slotIndex, bool pre);

    /** Logische Looper-Struktur cachen (Owner: EngineProcessor,
        applyLooperSettings) UND alle Big-Out-Nodes darauf syncen. */
    void setLooperStructure (const LooperBigOutModule::Structure& structure);

    /** Alle looper_big_out-Nodes auf die gecachte Struktur bringen:
        <Outputs> regenerieren, Kabel über Spec-Identität remappen
        (verschwundene Slots verlieren ihr Kabel, überlebende Kanäle
        werden alt→neu umgeschrieben), gefadete Re-Materialisierung.
        Undo-frei — die Struktur ist App-Zustand außerhalb des Undo-Trees;
        Reversibilität erzwungener Löschungen übernimmt der Papierkorb. */
    void syncLooperBigOutConfigs();

    /** true, wenn ein Kabel an einem Big-Out-Slot hängt, den das
        Entfernen von Track (trackIndex ≥ 0) bzw. Looper (trackIndex −1:
        alle Tracks + Bus) zerstören würde. Indizes 0-basiert. */
    [[nodiscard]] bool hasLooperBigOutCables (int looperIndex, int trackIndex) const;

    /** Betroffene Big-Out-Kabel einsammeln UND entfernen (undo-frei —
        Reversibilität übernimmt der Papierkorb). */
    std::vector<LooperBigOutModule::BigOutCableRef>
        collectAndRemoveBigOutCables (int looperIndex, int trackIndex);

    /** Papierkorb-Restore: Kabel spec-relativ neu anlegen (Kanal aus der
        DANN gültigen Slot-Liste; fehlende Nodes/Slots werden übersprungen).
        Liefert die Zahl der NICHT wiederherstellbaren Kabel. */
    int restoreBigOutCables (const std::vector<LooperBigOutModule::BigOutCableRef>& cables);

    /** Patch-Aktion (Dev-Modus 4.6): User-Regelbereich eines Parameters —
        Fader nutzt [userMin, userMax], der DSP clamped weiter auf die
        Hard-Range. Validiert userMin < userMax innerhalb der Hard-Range;
        der aktuelle Wert wird in derselben Undo-Transaktion in den neuen
        Bereich geclamped. Message Thread. */
    bool setParameterUserRange (const juce::String& nodeUuid, const juce::String& paramId,
                                double userMin, double userMax);

    /** Patch-Aktion (Dev-Modus 4.6): blendet einen dsp-Parameter aus/ein.
        Ausblenden trennt bestehende CV-Kabel des Parameters in DERSELBEN
        Undo-Transaktion (keine Phantom-Modulation); das Bus-Layout /
        numInputChannels bleibt IMMER unverändert. Message Thread. */
    bool setParameterHidden (const juce::String& nodeUuid, const juce::String& paramId,
                             bool hidden);

    /** Patch-Aktion (Dev-Modus 4.6): Fader-Response-Kurve eines dsp-
        Parameters — "x1 y1 x2 y2" (Bezier, ChassisSchema) oder leer für
        linear (Property entfernt). Undo-fähig; reines UI-Mapping, DSP/OSC
        unberührt. false bei unbekanntem/nicht-dsp-Parameter oder
        unlesbarem Kurven-String. Message Thread. */
    bool setParameterCurve (const juce::String& nodeUuid, const juce::String& paramId,
                            const juce::String& curveText);

    /** Patch-Aktion (Dev-Modus 4.6): Control-Link — targetParamId folgt
        sourceParamId (dsp-Parameter DESSELBEN Moduls) als interne
        Modulation mit bipolarem Amount (−1..+1, negativ = gegenläufig).
        Leere Quelle löscht den Link. Undo-fähig (eine Transaktion), wird
        live ins Modul gespiegelt — kein Rebuild. false bei unbekannten/
        nicht-dsp-Parametern oder Quelle == Ziel. Message Thread. */
    bool setParameterLink (const juce::String& nodeUuid, const juce::String& targetParamId,
                           const juce::String& sourceParamId, double amount);

    /** Patch-Aktion (Dev-Modus 4.6): Response-Kurve des Control-Links —
        formt die normalisierte Quelle (z.B. Gain-Matching). "x1 y1 x2 y2"
        oder leer für linear. Undo-fähig, live gespiegelt. Message Thread. */
    bool setParameterLinkCurve (const juce::String& nodeUuid, const juce::String& paramId,
                                const juce::String& curveText);

    /** Patch-Aktion (Dev-Modus 4.6): Fader↔Button-Modus eines dsp-Parameters.
        true → uiMode = "buttons" (Nicht-Dev-Ansicht zeigt Wert-Buttons statt
        Fader), false → Property entfernt (Fader). Die Button-Liste uiButtons
        bleibt beim Zurückschalten erhalten. Undo-fähig; DSP/OSC/CV unberührt.
        false bei unbekanntem/nicht-dsp-Parameter. Message Thread. */
    bool setParameterUiMode (const juce::String& nodeUuid, const juce::String& paramId,
                             bool useButtons);

    /** Patch-Aktion (Dev-Modus 4.6): Button-Anzahl 0..ChassisSchema::
        maxUiButtons. Wachsen hängt Buttons "P{n}" mit dem AKTUELLEN
        paramValue (geclamped auf die Hard-Range) an, Schrumpfen entfernt von
        hinten; 0 entfernt das Property. EIN Undo restauriert die komplette
        Liste. false bei nicht-dsp/unbekanntem Parameter oder count außerhalb
        des Limits. Message Thread. */
    bool setParameterButtonCount (const juce::String& nodeUuid, const juce::String& paramId,
                                  int count);

    //==========================================================================
    // Macro-Modulation (MIDI-Rig M5c, ParamModulation.h): der Tree behält
    // den BASISWERT, der Bus schreibt den Effektivwert (Basis + Offset ·
    // User-Range, doppelt geclamped) über syncParameterValue() in das
    // bestehende getParameterTarget()-Atomic. Einträge überleben Node-
    // Delete/Undo bewusst (Uuid-keyed, Store dann No-op — Re-Apply beim
    // Rebuild); gelöscht wird über clearParamModulation (Target-Dtor).

    void setParamModulation (const ParamModKey& key, float offsetNorm) override;
    void clearParamModulation (const ParamModKey& key) override;

    /** Effektivwert für die Modulations-Anzeige (Tree-Basis + Offset,
        OHNE Modul-Zugriff — zombiesicher per Konstruktion); kein Eintrag
        oder Parameter nicht (mehr) im Tree → kein Wert. [MT] */
    [[nodiscard]] std::optional<float>
        getParamModulationEffective (const juce::String& nodeUuid,
                                     const juce::String& paramId) const;

    /** Patch-Aktion (Dev-Modus 4.6, Kern-Workflow): speichert den AKTUELLEN
        paramValue (geclamped auf die Hard-Range) in Button buttonIndex.
        Undo-fähig. false bei ungültigem Index/Parameter. Message Thread. */
    bool storeParameterButtonValue (const juce::String& nodeUuid, const juce::String& paramId,
                                    int buttonIndex);

    /** Patch-Aktion (Dev-Modus 4.6): benennt Button buttonIndex um. Name wird
        getrimmt; false bei leerem Ergebnis, > ChassisSchema::
        maxUiButtonNameLength Zeichen oder ungültigem Index. Message Thread. */
    bool renameParameterButton (const juce::String& nodeUuid, const juce::String& paramId,
                                int buttonIndex, const juce::String& newName);

    /** Dev-Modus 4.6: aktuelle dsp-Overrides des Nodes (userMin/userMax/
        uiHidden/curve/uiMode/uiButtons) als Modul-Typ-Defaults sichern —
        greift bei künftigen Neu-Anlagen dieses factoryIds. false ohne
        ModuleUiDefaults/Node. */
    bool captureModuleUiDefaults (const juce::String& nodeUuid);

    /** Phase 1 des zweiphasigen Deletes (5.3): setzt nodeState → Deleting.
        false, wenn kein Node mit dieser nodeId existiert oder der Node ein
        externer Endpunkt ist (I/O-Nodes sind nicht löschbar). Message Thread. */
    [[nodiscard]] bool requestNodeDelete (const juce::String& nodeUuid);

    //==========================================================================
    /** Patch-Aktion: legt ein Kabel als Connection-Child an (Schema 6.2),
        undo-fähig. false bei unbekanntem Endpunkt, Selbstverbindung oder
        Duplikat. Die Kanal-Validierung übernimmt der Graph beim Swap. */
    bool addConnection (const juce::String& sourceUuid, int sourceChannel,
                        const juce::String& destUuid, int destChannel);

    /** Patch-Aktion: entfernt das passende Kabel undo-fähig.
        false, wenn keines existiert. */
    bool removeConnection (const juce::String& sourceUuid, int sourceChannel,
                           const juce::String& destUuid, int destChannel);

    /** Stereo-Paar-Kabel in EINER Undo-Transaktion: (sourceChannel →
        destChannel) plus (sourceChannel+1 → destChannel+1). Das zweite Kabel
        entsteht nur, wenn destChannel+1 am Ziel existiert (numInputChannels)
        und die Verbindung noch frei ist — sonst bleibt nur das erste
        (Mono-Fallback, dokumentierter Randfall). false, wenn schon das
        erste Kabel ungültig wäre. */
    bool addConnectionPair (const juce::String& sourceUuid, int sourceChannel,
                            const juce::String& destUuid, int destChannel);

    /** Trennt beide Kabel eines Stereo-Paars in EINER Undo-Transaktion
        (das zweite nur, falls vorhanden). false, wenn keines existiert. */
    bool removeConnectionPair (const juce::String& sourceUuid, int sourceChannelA,
                               const juce::String& destUuid, int destChannelA,
                               int sourceChannelB, int destChannelB);

    //==========================================================================
    /** Registriert einen Hardware-ANKER (die AudioGraphIOProcessor des
        EngineProcessor) für eine Endpunkt-factoryId (audio_input/
        audio_output). Seit ADR 009 sind I/O-Nodes reguläre Module
        (AudioEndpointModule) — der Anker ist nur noch das Ziel der
        impliziten Anker-Kabel, die der Manager bei der Materialisierung
        eines IExternalAudioEndpoint zieht. Ohne Registrierung (Tests)
        bleiben die Proxys unverbunden. */
    void registerExternalEndpoint (const juce::String& moduleId,
                                   juce::AudioProcessorGraph::NodeID graphNodeId);

    /** true, wenn die factoryId ein Hardware-Anker-Typ ist (registriert). */
    [[nodiscard]] bool isExternalEndpoint (const juce::String& moduleId) const noexcept;

    /** Factory-Schlüssel mit Migrations-Fallback: alte Bestände (vor der
        factoryId/moduleId-Trennung) tragen den Schlüssel in moduleId.
        Public — auch die UI unterscheidet Modultypen darüber. */
    [[nodiscard]] static juce::String factoryKeyOf (const juce::ValueTree& nodeTree);

    //==========================================================================
    /** Takt-Verteiler für IClockSlave-Module — wird bei der Materialisierung
        injiziert (5.2 Schritt 1, vor der Graph-Aufnahme). Der Bus muss jedes
        Modul überdauern (Owner: EngineProcessor). nullptr → Slaves laufen
        frei (Tests). Message Thread, vor den ersten addModuleNode-Aufrufen. */
    void setClockBus (const ClockBus* bus) noexcept;

    /** Link-Audio-Kontext für ILinkAudioClient-Module (7.2) — wird bei der
        Materialisierung VOR prepareForGraph injiziert (der Sink entsteht in
        prepareToPlay und braucht Clock + moduleId). Die Clock muss jedes
        Modul überdauern (Owner: EngineProcessor). nullptr → Module bleiben
        ohne Session-Kanal (Tests). Message Thread. */
    void setLinkClock (LinkClock* clock) noexcept;

    /** Capture-Kontext für ICaptureTapClient-Module — wird bei der
        Materialisierung VOR prepareForGraph injiziert (die Kanal-
        Registrierung passiert dort, Spurname == moduleId). Der Service muss
        jedes Modul überdauern (Owner: EngineProcessor, VOR dem Graph
        deklariert). nullptr → Module bleiben reines Pass-Through (Tests).
        Message Thread. */
    void setCaptureService (CaptureService* service) noexcept;

    /** Looper-Busse für ILooperAudioClient-Module (Looper Out) — bei der
        Materialisierung VOR prepareForGraph injiziert. Die Bank muss jedes
        Modul überdauern (Owner: EngineProcessor, VOR dem Graph deklariert).
        nullptr → Module geben Stille aus (Tests). Message Thread. */
    void setLooperBank (LooperBank* bank) noexcept;

    /** Kanal-Namen (App-Zustand) für das Auto-Naming der Send-Kanäle: die
        Quelle eines Eingangs, die am audio_input-Endpunkt hängt, liefert ihr
        ChannelNames-Label. nullptr → Fallback "In N" (Tests). Message Thread. */
    void setChannelNames (ChannelNames* names) noexcept;

    /** Modul-Typ-Defaults (Dev-Modus 4.6, Owner: EngineProcessor) —
        addModuleNode wendet sie bei Neu-Anlagen als Overlay an.
        nullptr → keine Defaults (Tests). Message Thread. */
    void setModuleUiDefaults (ModuleUiDefaults* defaults) noexcept;

    /** Seiten-Verwaltung (ADR 008 M1, Owner: EngineProcessor) —
        addModuleNode setzt die pageUuid der aktiven Seite auf neue Nodes.
        nullptr → Nodes ohne pageUuid (Tests; migrateAndRepair zieht nach).
        Message Thread. */
    void setPageManager (PageManager* pages) noexcept;

    //==========================================================================
    /** Auto-Naming (7.2 Schritt 3): übernimmt für ALLE Eingänge eines Send-
        Nodes den aktuellen Quell-Namen als autoName (userName bleibt), in
        einer Undo-Transaktion. false bei unbekanntem/fremdem Node.
        Message Thread. */
    bool refreshAutoNames (const juce::String& nodeUuid);

    //==========================================================================
    /** Live-Modul-Instanz zu einer Tree-nodeId — nullptr solange das Modul
        noch nicht materialisiert ist (5.2 Schritt 1–3) oder nodeError gesetzt
        wurde. NUR Message Thread; der Pointer ist bis zum nächsten Swap
        gültig (der OscController nutzt ihn zur Endpoint-Auflösung, 7.1). */
    [[nodiscard]] ConduitModule* getModuleFor (const juce::String& nodeUuid) const;

    //==========================================================================
    /** true, solange ein gesammeltes Frame-Delta auf den Rebuild wartet. */
    [[nodiscard]] bool isTopologyDirty() const noexcept;

    /** true zwischen Fade-Out-Start (Schritt 2) und Topologie-Swap (Schritt 3). */
    [[nodiscard]] bool isWaitingForSilence() const noexcept;

    /** Anzahl bisher ausgeführter (coalesced) Rebuilds — für Tests/Diagnose. */
    [[nodiscard]] int getRebuildCount() const noexcept;

    /** Führt einen ausstehenden Verarbeitungsschritt sofort synchron aus.
        Entspricht dem nächsten Message-Loop-Durchlauf — für Tests/Shutdown. */
    void flushPendingTopologyUpdate();

private:
    //==========================================================================
    // juce::ValueTree::Listener — Topologie-Änderungen + Phase 1 des Deletes
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int formerIndex) override;
    void valueTreeRedirected (juce::ValueTree& tree) override;

    // juce::AsyncUpdater — Fade-Zustandsmaschine (5.2) + Coalescing (5.5)
    void handleAsyncUpdate() override;

    //==========================================================================
    /** Phase 2 des Deletes (5.3): entfernt Subtree + Kabel, sobald die UI
        ihre Referenzen freigegeben hat (Refcount 0). */
    void processPendingDeletes();

    /** Schritt 1: instanziiert + prepariert alle neuen Tree-Nodes VOR dem
        Fade-Out. Fehler → nodeError, das Modul wird übersprungen. */
    void prepareNewModules();

    /** Factory-Create + prepareForGraph für einen Tree-Node.
        Bei Fehler: setzt nodeError und gibt nullptr zurück. */
    [[nodiscard]] std::unique_ptr<ConduitModule> materializeModule (juce::ValueTree nodeTree);

    /** Schritt 3: konsumiert das gesamte Frame-Delta in einem Swap. */
    void performTopologySwap();

    void removeVanishedNodes();
    void addNewNodes();
    void syncConnections();

    /** Tree → Atomic (Dual-State-Gegenstück zu 6.1): spiegelt paramValue auf
        das Echtzeit-Target des live-Moduls — bedient UI-Slider, Preset-Load
        und Undo. Kein Rebuild, nur ein atomic store. M5c: komponiert einen
        aktiven Macro-Modulations-Offset mit ein (Tree behält die Basis). */
    void syncParameterValue (juce::ValueTree parameterTree);

    /** Parameter-Subtree zu (nodeUuid, paramId) — ungültig wenn Node oder
        Parameter fehlt (M5c-Modulation, Effektivwert-Anzeige). */
    [[nodiscard]] juce::ValueTree parameterTreeFor (const juce::String& nodeUuid,
                                                    const juce::String& paramId) const;

    /** Tree → Atomic (Dev-Modus 4.6): spiegelt den effektiven User-
        Regelbereich (userMin/userMax, Fallback Hard-Range) ins Chassis —
        Wirkbereich der CV-Modulation. Kein Rebuild. */
    static void syncParameterUserRange (const juce::ValueTree& parameterTree,
                                        ProcessorModule& processor);

    /** Tree → Atomic (Dev-Modus 4.6): spiegelt den Control-Link
        (linkSource/linkAmount) ins Chassis. Kein Rebuild. */
    static void syncParameterLink (const juce::ValueTree& parameterTree,
                                   ProcessorModule& processor);

    [[nodiscard]] bool isManagedGraphNode (juce::AudioProcessorGraph::NodeID nodeId) const;
    [[nodiscard]] bool isExternalGraphNode (juce::AudioProcessorGraph::NodeID nodeId) const;

    /** Anker-Kabel eines I/O-Endpunkt-Moduls ziehen (ADR 009): Hardware-
        Anker ↔ Proxy, kanalweise; kein Patch-Zustand (nicht im Tree). */
    void connectEndpointAnchor (juce::AudioProcessorGraph::NodeID proxyNodeId,
                                IExternalAudioEndpoint& endpoint);

    [[nodiscard]] juce::ValueTree findConnectionTree (const juce::String& sourceUuid, int sourceChannel,
                                                      const juce::String& destUuid, int destChannel) const;

    /** Gemeinsame Kabel-Validierung von addConnection/addConnectionPair
        (Endpunkte existieren, keine Selbstverbindung, kein Duplikat). */
    [[nodiscard]] bool canConnect (const juce::String& sourceUuid, int sourceChannel,
                                   const juce::String& destUuid, int destChannel) const;

    /** Hängt das Connection-Child ein — OHNE eigene Transaktion (der Aufrufer
        klammert; addConnectionPair bündelt zwei Kabel in einer). */
    void appendConnectionChild (const juce::String& sourceUuid, int sourceChannel,
                                const juce::String& destUuid, int destChannel);

    /** Auto-Naming-Snapshot: setzt für den Eingang, der destChannel enthält,
        autoName aus der Quelle. followSource=false (Link-Send): nur wenn
        userName UND autoName leer sind (Einmal-Snapshot); followSource=true
        (Looper-In): autoName folgt der Quelle bei jedem Stecken, userName
        gewinnt, Kollisionen bekommen " 2"-Suffixe. Non-undoable. */
    void snapshotAutoName (juce::ValueTree sendNodeTree, int destChannel,
                           bool followSource);

    /** Ketten-Namen ALLER Looper-In-Slots nachziehen (jede Kabel-Änderung —
        auch Upstream-Umstecken ändert "{quelle} · {fx}"-Ketten). */
    void refreshLooperInAutoNames();

    /** true für die Container Nodes[] und Connections[] (Schema 6.2). */
    [[nodiscard]] static bool isTopologyContainer (const juce::ValueTree& tree) noexcept;

    /** Migration: fehlendes factoryId aus moduleId ergänzen (alter Zustand). */
    static void normalizeNode (juce::ValueTree nodeTree);

    [[nodiscard]] static juce::String sanitizeModuleName (const juce::String& raw);
    [[nodiscard]] bool isModuleNameTaken (const juce::String& name) const;
    [[nodiscard]] juce::String makeUniqueModuleName (const juce::String& factoryKey) const;

    void markTopologyDirty();

    //==========================================================================
    enum class SwapPhase
    {
        idle,               // kein Swap aktiv
        waitingForSilence   // Fade-Out läuft, Swap wartet auf den Audio Thread
    };

    juce::ValueTree rootState;          // ref-counted Handle
    juce::AudioProcessorGraph& graph;
    GraphFader& fader;
    ModuleFactory& factory;
    juce::UndoManager& undoManager;
    NodeUiRegistry& uiRegistry;

    // Nur Message Thread — bewusst kein std::atomic (kein Cross-Thread-Zugriff)
    SwapPhase swapPhase = SwapPhase::idle;
    bool topologyDirty = false;
    int pendingChangeCount = 0;
    int rebuildCount = 0;

    // Tree-nodeId (Uuid-String) → Graph-NodeID der tree-verwalteten Nodes
    std::map<juce::String, juce::AudioProcessorGraph::NodeID> treeToGraphNode;

    // In Schritt 1 vorbereitete Instanzen, warten auf den Swap
    std::map<juce::String, std::unique_ptr<ConduitModule>> preparedModules;

    // Deleting-Nodes (Phase 1 abgeschlossen): nodeId → gecachte moduleId
    std::map<juce::String, juce::String> pendingDeletes;

    // Looper-I/O-Nodes mit geänderter Slot-Konfiguration (Kanalzahl-
    // Property): im NÄCHSTEN gefadeten Swap re-materialisieren — anders
    // als der harte Endpunkt-Pfad (Gerätewechsel) läuft das am spielenden
    // System und gehört deshalb HINTER den Fade-Out (5.2).
    std::vector<juce::String> pendingRematerialize;

    // M5c: aktive Macro-Modulationen (Offset [-1..+1] pro Parameter),
    // Uuid-keyed — kein Modul-Pointer (5.3). Nur Message Thread.
    std::map<ParamModKey, float> paramModulations;

    // Reservierte moduleIds → extern verwaltete Graph-Nodes (I/O)
    std::map<juce::String, juce::AudioProcessorGraph::NodeID> externalEndpoints;

    // Takt-Verteiler für IClockSlaves (Owner: EngineProcessor)
    const ClockBus* clockBus = nullptr;

    // Link-Audio-Kontext für ILinkAudioClients (Owner: EngineProcessor)
    LinkClock* linkClock = nullptr;

    // Capture-Kontext für ICaptureTapClients (Owner: EngineProcessor)
    CaptureService* captureService = nullptr;

    // Looper-Busse für ILooperAudioClients (Owner: EngineProcessor)
    LooperBank* looperBank = nullptr;

    // Logische Looper-Struktur (Cache, setLooperStructure) — Quelle für
    // die Auto-Follow-Outputs der Big-Out-Nodes
    LooperBigOutModule::Structure looperStructure;

    // Kanal-Namen für Send-Auto-Naming (Owner: EngineProcessor)
    ChannelNames* channelNames = nullptr;

    // Modul-Typ-Defaults des Dev-Modus (Owner: EngineProcessor)
    ModuleUiDefaults* uiDefaults = nullptr;

    // Seiten-Verwaltung (ADR 008 M1, Owner: EngineProcessor)
    PageManager* pageManager = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphManager)
};

} // namespace conduit
