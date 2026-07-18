#include "GraphManager.h"

#include <algorithm>
#include <vector>

#include "Interfaces/ICaptureTapClient.h"
#include "Interfaces/IClockSlave.h"
#include "Interfaces/IExternalAudioEndpoint.h"
#include "Interfaces/ILinkAudioClient.h"
#include "Interfaces/ISendConfigClient.h"
#include "Interfaces/IStochastic.h"
#include "ModuleUiDefaults.h"
#include "Modules/ChassisSchema.h"
#include "Modules/LinkAudioReceiveModule.h"
#include "Modules/LinkAudioSendModule.h"
#include "Modules/ModuleFactory.h"
#include "Modules/ProcessorModule.h"
#include "NodeUiRegistry.h"
#include "PageManager.h"
#include "SourceNameResolver.h"

namespace conduit
{

GraphManager::GraphManager (juce::ValueTree rootTree,
                            juce::AudioProcessorGraph& processorGraph,
                            GraphFader& faderToUse,
                            ModuleFactory& factoryToUse,
                            juce::UndoManager& undoManagerToUse,
                            NodeUiRegistry& uiRegistryToUse)
    : rootState (std::move (rootTree)),
      graph (processorGraph),
      fader (faderToUse),
      factory (factoryToUse),
      undoManager (undoManagerToUse),
      uiRegistry (uiRegistryToUse)
{
    rootState.addListener (this);

    // Gibt die letzte UI-Component einen Deleting-Node frei, kann eine
    // wartende Phase 2 sofort weiterlaufen (5.3)
    uiRegistry.setOnNodeFullyReleased ([this] (const juce::String&) { triggerAsyncUpdate(); });
}

GraphManager::~GraphManager()
{
    uiRegistry.setOnNodeFullyReleased (nullptr);
    rootState.removeListener (this);
    cancelPendingUpdate();  // AsyncUpdater darf nie auf ein zerstörtes Objekt feuern
}

//==============================================================================
bool GraphManager::isTopologyDirty() const noexcept     { return topologyDirty; }
bool GraphManager::isWaitingForSilence() const noexcept { return swapPhase == SwapPhase::waitingForSilence; }
int GraphManager::getRebuildCount() const noexcept      { return rebuildCount; }

void GraphManager::flushPendingTopologyUpdate()
{
    handleUpdateNowIfNeeded();
}

//==============================================================================
ConduitModule* GraphManager::getModuleFor (const juce::String& nodeUuid) const
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (const auto it = treeToGraphNode.find (nodeUuid); it != treeToGraphNode.end())
        if (const auto graphNode = graph.getNodeForId (it->second))
            return dynamic_cast<ConduitModule*> (graphNode->getProcessor());

    return nullptr;
}

//==============================================================================
juce::ValueTree GraphManager::addModuleNode (const juce::String& factoryKey, juce::Point<int> position,
                                             const std::function<void (juce::ValueTree&)>& configure)
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Instanz nur für createState() (4.4) — die Graph-Instanz erzeugt der
    // Swap später frisch über prepareNewModules().
    const auto module = factory.create (factoryKey);

    if (module == nullptr)
        return {};

    auto nodeTree = module->createState();

    // Anlege-Konfiguration (z.B. Send-Eingangszahl) vor dem Einhängen —
    // der Node materialisiert dann direkt mit dem konfigurierten Layout.
    if (configure)
        configure (nodeTree);

    nodeTree.setProperty (id::positionX, position.x, nullptr);
    nodeTree.setProperty (id::positionY, position.y, nullptr);

    // Modul-Typ-Defaults (Dev-Modus 4.6): Overlay NUR bei Neu-Anlage —
    // Presets/Patches bleiben unberührt (dort steht der Zustand im Tree)
    if (uiDefaults != nullptr)
        uiDefaults->applyTo (nodeTree);

    // Eindeutige named_id (OSC-Pfad, 7) — vor dem Einhängen, ohne Listener
    nodeTree.setProperty (id::moduleId, makeUniqueModuleName (factoryKey), nullptr);

    // Seite des neuen Nodes (ADR 008 M1) — die aktive Seite; ohne
    // PageManager (Tests) zieht migrateAndRepair() die Property nach
    if (pageManager != nullptr)
        nodeTree.setProperty (id::pageUuid, pageManager->getActivePageUuid(), nullptr);

    undoManager.beginNewTransaction ("Modul hinzufügen");
    rootState.getChildWithName (id::nodes).appendChild (nodeTree, &undoManager);
    return nodeTree;
}

bool GraphManager::renameNode (const juce::String& nodeUuid, const juce::String& requestedName)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);

    if (! nodeTree.isValid())
        return false;

    const auto sanitized = sanitizeModuleName (requestedName);

    if (sanitized.isEmpty())
        return false;

    if (sanitized == nodeTree.getProperty (id::moduleId).toString())
        return true;  // No-op

    if (isModuleNameTaken (sanitized))
        return false;

    // Rename ändert OSC-Pfade — patchbare Aktion, also undo-fähig
    undoManager.beginNewTransaction ("Modul umbenennen");
    nodeTree.setProperty (id::moduleId, sanitized, &undoManager);
    return true;
}

bool GraphManager::setParameterUserRange (const juce::String& nodeUuid, const juce::String& paramId,
                                          double userMin, double userMax)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);
    auto param = nodeTree.getChildWithName (id::parameters)
                     .getChildWithProperty (id::paramId, paramId);

    if (! param.isValid())
        return false;

    const auto hardMin = (double) param.getProperty (id::paramMin, 0.0);
    const auto hardMax = (double) param.getProperty (id::paramMax, 1.0);

    // User-Bereich muss echt und innerhalb der Hard-Range liegen
    if (! (userMin < userMax) || userMin < hardMin || userMax > hardMax)
        return false;

    undoManager.beginNewTransaction ("Regelbereich ändern");
    param.setProperty (id::paramUserMin, userMin, &undoManager);
    param.setProperty (id::paramUserMax, userMax, &undoManager);

    // Wert in den neuen Bereich clampen — Teil DERSELBEN Transaktion,
    // sonst restauriert Undo die Range, aber nicht den Wert
    const auto value   = (double) param.getProperty (id::paramValue, 0.0);
    const auto clamped = juce::jlimit (userMin, userMax, value);

    if (! juce::exactlyEqual (value, clamped))
        param.setProperty (id::paramValue, clamped, &undoManager);

    return true;
}

bool GraphManager::setParameterHidden (const juce::String& nodeUuid, const juce::String& paramId,
                                       bool hidden)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);
    auto param = nodeTree.getChildWithName (id::parameters)
                     .getChildWithProperty (id::paramId, paramId);

    // Nur dsp-Parameter sind ausblendbar (Gains/Attenuverter sind Standard)
    if (! param.isValid()
        || ChassisSchema::roleOf (param) != juce::String (ChassisSchema::roleDsp))
        return false;

    if ((bool) param.getProperty (id::paramUiHidden, false) == hidden)
        return true;  // No-op

    undoManager.beginNewTransaction (hidden ? "Parameter ausblenden"
                                            : "Parameter einblenden");
    param.setProperty (id::paramUiHidden, hidden, &undoManager);

    // Ausblenden trennt CV-Kabel des Parameters in derselben Transaktion
    // (User-Entscheidung: keine unsichtbare Phantom-Modulation). Das
    // Bus-Layout bleibt unverändert (4.6).
    if (hidden)
    {
        const auto cvChannel = ChassisSchema::cvChannelForParam (nodeTree, paramId);
        auto connections = rootState.getChildWithName (id::connections);

        for (int i = connections.getNumChildren(); --i >= 0;)
        {
            const auto connection = connections.getChild (i);

            if (connection.getProperty (id::destNodeId).toString() == nodeUuid
                && (int) connection.getProperty (id::destChannel) == cvChannel)
                connections.removeChild (i, &undoManager);
        }
    }

    return true;
}

bool GraphManager::setParameterCurve (const juce::String& nodeUuid, const juce::String& paramId,
                                      const juce::String& curveText)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);
    auto param = nodeTree.getChildWithName (id::parameters)
                     .getChildWithProperty (id::paramId, paramId);

    // Nur dsp-Parameter tragen Fader-Kurven; nicht-leere Strings müssen
    // parsen (Monotonie-Garantie via parseCurve-Clamping)
    if (! param.isValid()
        || ChassisSchema::roleOf (param) != juce::String (ChassisSchema::roleDsp)
        || (curveText.isNotEmpty() && ! ChassisSchema::parseCurve (curveText).has_value()))
        return false;

    undoManager.beginNewTransaction ("Fader-Kurve ändern");

    if (curveText.isEmpty())
        param.removeProperty (id::paramCurve, &undoManager);   // linear
    else
        param.setProperty (id::paramCurve, curveText, &undoManager);

    return true;
}

bool GraphManager::setParameterLink (const juce::String& nodeUuid, const juce::String& targetParamId,
                                     const juce::String& sourceParamId, double amount)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);
    const auto params = nodeTree.getChildWithName (id::parameters);
    auto target = params.getChildWithProperty (id::paramId, targetParamId);

    if (! target.isValid()
        || ChassisSchema::roleOf (target) != juce::String (ChassisSchema::roleDsp)
        || sourceParamId == targetParamId)
        return false;

    // Nicht-leere Quelle muss ein dsp-Parameter DESSELBEN Moduls sein
    if (sourceParamId.isNotEmpty())
    {
        const auto source = params.getChildWithProperty (id::paramId, sourceParamId);

        if (! source.isValid()
            || ChassisSchema::roleOf (source) != juce::String (ChassisSchema::roleDsp))
            return false;
    }

    undoManager.beginNewTransaction (sourceParamId.isEmpty() ? "Control-Link lösen"
                                                             : "Control-Link setzen");

    if (sourceParamId.isEmpty())
    {
        target.removeProperty (id::paramLinkSource, &undoManager);
        target.removeProperty (id::paramLinkAmount, &undoManager);
    }
    else
    {
        target.setProperty (id::paramLinkSource, sourceParamId, &undoManager);
        target.setProperty (id::paramLinkAmount, juce::jlimit (-1.0, 1.0, amount), &undoManager);
    }

    return true;
}

bool GraphManager::setParameterLinkCurve (const juce::String& nodeUuid, const juce::String& paramId,
                                          const juce::String& curveText)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);
    auto param = nodeTree.getChildWithName (id::parameters)
                     .getChildWithProperty (id::paramId, paramId);

    if (! param.isValid()
        || ChassisSchema::roleOf (param) != juce::String (ChassisSchema::roleDsp)
        || (curveText.isNotEmpty() && ! ChassisSchema::parseLinkResponse (curveText).has_value()))
        return false;

    undoManager.beginNewTransaction ("Link-Kurve ändern");

    if (curveText.isEmpty())
        param.removeProperty (id::paramLinkCurve, &undoManager);   // linear
    else
        param.setProperty (id::paramLinkCurve, curveText, &undoManager);

    return true;
}

namespace
{
    /** Button-Liste eines Parameters (fehlendes/kaputtes Property = leer). */
    [[nodiscard]] std::vector<ChassisSchema::ButtonPreset> buttonsOf (const juce::ValueTree& param)
    {
        return ChassisSchema::parseButtons (param.getProperty (id::paramUiButtons).toString())
            .value_or (std::vector<ChassisSchema::ButtonPreset>{});
    }

    /** Aktueller paramValue, geclamped auf die Hard-Range (min/max). */
    [[nodiscard]] double clampedParamValueOf (const juce::ValueTree& param)
    {
        return juce::jlimit ((double) param.getProperty (id::paramMin, 0.0),
                             (double) param.getProperty (id::paramMax, 1.0),
                             (double) param.getProperty (id::paramValue, 0.0));
    }
} // namespace

bool GraphManager::setParameterUiMode (const juce::String& nodeUuid, const juce::String& paramId,
                                       bool useButtons)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);
    auto param = nodeTree.getChildWithName (id::parameters)
                     .getChildWithProperty (id::paramId, paramId);

    if (! param.isValid()
        || ChassisSchema::roleOf (param) != juce::String (ChassisSchema::roleDsp))
        return false;

    // No-op ohne Transaktion (Muster setParameterHidden). Beim Zurückschalten
    // zählt hasProperty, nicht isButtonMode — ein handeditiertes uiMode="fader"
    // soll trotzdem entfernt werden.
    if (useButtons ? ChassisSchema::isButtonMode (param)
                   : ! param.hasProperty (id::paramUiMode))
        return true;

    undoManager.beginNewTransaction ("Button-Modus umschalten");

    if (useButtons)
        param.setProperty (id::paramUiMode, ChassisSchema::uiModeButtons, &undoManager);
    else
        param.removeProperty (id::paramUiMode, &undoManager);   // uiButtons bleibt geparkt

    return true;
}

bool GraphManager::setParameterButtonCount (const juce::String& nodeUuid, const juce::String& paramId,
                                            int count)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);
    auto param = nodeTree.getChildWithName (id::parameters)
                     .getChildWithProperty (id::paramId, paramId);

    if (! param.isValid()
        || ChassisSchema::roleOf (param) != juce::String (ChassisSchema::roleDsp)
        || count < 0 || count > ChassisSchema::maxUiButtons)
        return false;

    auto buttons = buttonsOf (param);

    if (static_cast<int> (buttons.size()) == count)
        return true;   // No-op

    undoManager.beginNewTransaction ("Button-Anzahl ändern");

    if (static_cast<int> (buttons.size()) > count)
    {
        buttons.resize (static_cast<size_t> (count));   // von hinten entfernen
    }
    else
    {
        const auto value = clampedParamValueOf (param);

        while (static_cast<int> (buttons.size()) < count)
            buttons.push_back ({ "P" + juce::String (buttons.size() + 1), value });
    }

    if (buttons.empty())
        param.removeProperty (id::paramUiButtons, &undoManager);
    else
        param.setProperty (id::paramUiButtons,
                           ChassisSchema::buttonsToString (buttons), &undoManager);

    return true;
}

bool GraphManager::storeParameterButtonValue (const juce::String& nodeUuid, const juce::String& paramId,
                                              int buttonIndex)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);
    auto param = nodeTree.getChildWithName (id::parameters)
                     .getChildWithProperty (id::paramId, paramId);

    if (! param.isValid()
        || ChassisSchema::roleOf (param) != juce::String (ChassisSchema::roleDsp))
        return false;

    auto buttons = buttonsOf (param);

    if (buttonIndex < 0 || buttonIndex >= static_cast<int> (buttons.size()))
        return false;

    const auto value = clampedParamValueOf (param);
    auto& target = buttons[static_cast<size_t> (buttonIndex)];

    // Float-Vergleich bewusst über float (6.1-Falle: var speichert double)
    if (juce::exactlyEqual (static_cast<float> (target.value), static_cast<float> (value)))
        return true;   // No-op

    undoManager.beginNewTransaction ("Wert in Button speichern");
    target.value = value;
    param.setProperty (id::paramUiButtons, ChassisSchema::buttonsToString (buttons), &undoManager);

    return true;
}

bool GraphManager::renameParameterButton (const juce::String& nodeUuid, const juce::String& paramId,
                                          int buttonIndex, const juce::String& newName)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);
    auto param = nodeTree.getChildWithName (id::parameters)
                     .getChildWithProperty (id::paramId, paramId);

    if (! param.isValid()
        || ChassisSchema::roleOf (param) != juce::String (ChassisSchema::roleDsp))
        return false;

    auto buttons = buttonsOf (param);
    const auto trimmed = newName.trim();

    if (buttonIndex < 0 || buttonIndex >= static_cast<int> (buttons.size())
        || trimmed.isEmpty() || trimmed.length() > ChassisSchema::maxUiButtonNameLength)
        return false;

    auto& target = buttons[static_cast<size_t> (buttonIndex)];

    if (target.name == trimmed)
        return true;   // No-op

    undoManager.beginNewTransaction ("Button umbenennen");
    target.name = trimmed;
    param.setProperty (id::paramUiButtons, ChassisSchema::buttonsToString (buttons), &undoManager);

    return true;
}

bool GraphManager::setNodeColour (const juce::String& nodeUuid, juce::uint32 colour)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);

    if (! nodeTree.isValid())
        return false;

    if ((juce::uint32) (int) nodeTree.getProperty (id::nodeColour, 0) == colour)
        return true;  // No-op

    undoManager.beginNewTransaction (colour != 0 ? "Modul-Farbe ändern"
                                                 : "Modul-Farbe entfernen");

    if (colour != 0)
        nodeTree.setProperty (id::nodeColour, (int) colour, &undoManager);
    else
        nodeTree.removeProperty (id::nodeColour, &undoManager);  // 0 = keine Property

    return true;
}

bool GraphManager::setLinkSendEnabled (const juce::String& nodeUuid, bool enabled)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);

    if (! nodeTree.isValid())
        return false;

    if ((bool) nodeTree.getProperty (id::linkSendEnabled, false) == enabled)
        return true;  // No-op

    // Ändert die announcten Link-Kanäle der Session — patchbare Aktion (4.6)
    undoManager.beginNewTransaction (enabled ? "Link-Send aktivieren"
                                             : "Link-Send deaktivieren");
    nodeTree.setProperty (id::linkSendEnabled, enabled, &undoManager);
    return true;
}

//==============================================================================
juce::String GraphManager::sanitizeModuleName (const juce::String& raw)
{
    // OSC-pfadtauglich (7): lowercase, [a-z0-9_]; Trenner werden '_'
    juce::String result;
    result.preallocateBytes (static_cast<size_t> (raw.length()));

    for (const auto character : raw.trim().toLowerCase())
    {
        if ((character >= 'a' && character <= 'z')
            || (character >= '0' && character <= '9') || character == '_')
            result << character;
        else if (character == ' ' || character == '-')
            result << '_';
    }

    return result;
}

bool GraphManager::isModuleNameTaken (const juce::String& name) const
{
    const auto nodesTree = rootState.getChildWithName (id::nodes);

    for (int i = 0; i < nodesTree.getNumChildren(); ++i)
        if (nodesTree.getChild (i).getProperty (id::moduleId).toString() == name)
            return true;

    return false;
}

juce::String GraphManager::makeUniqueModuleName (const juce::String& factoryKey) const
{
    for (int counter = 1;; ++counter)
        if (const auto candidate = factoryKey + "_" + juce::String (counter);
            ! isModuleNameTaken (candidate))
            return candidate;
}

juce::String GraphManager::factoryKeyOf (const juce::ValueTree& nodeTree)
{
    if (nodeTree.hasProperty (id::factoryId))
        return nodeTree.getProperty (id::factoryId).toString();

    return nodeTree.getProperty (id::moduleId).toString();  // Alt-Bestand
}

void GraphManager::normalizeNode (juce::ValueTree nodeTree)
{
    // Migration alter States: moduleId trug früher den Factory-Schlüssel
    if (! nodeTree.hasProperty (id::factoryId))
        nodeTree.setProperty (id::factoryId,
                              nodeTree.getProperty (id::moduleId).toString(), nullptr);

    // Modul-spezifische Migration: Multi-Input-Send-Schema (stateVersion 1→2)
    if (factoryKeyOf (nodeTree) == LinkAudioSendModule::staticModuleId)
        LinkAudioSendModule::migrate (nodeTree);

    // FX-Chassis-Migration (4.6, stateVersion 1→2): gilt für ALLE
    // Processor-Nodes — ergänzt Gains, Attenuverter, role und CV-Kanäle.
    // Idempotent, frisch angelegte Nodes sind bereits chassis-förmig.
    if (nodeTree.getProperty (id::type).toString() == toString (ModuleType::processor))
        ChassisSchema::migrate (nodeTree);
}

//==============================================================================
bool GraphManager::requestNodeDelete (const juce::String& nodeUuid)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);

    // Auch I/O-Endpunkte sind löschbar (ADR 009 — keine Reserved-Sperre;
    // Patch ohne Output = bewusste Stille)
    if (! nodeTree.isValid())
        return false;

    // Phase 1: UI entkoppelt sich über ihren nodeState-Listener.
    // Bewusst ohne UndoManager — undo-fähig ist die Subtree-Entfernung
    // in Phase 2, nicht der Übergangszustand.
    nodeTree.setProperty (id::nodeState, toString (NodeState::deleting), nullptr);
    return true;
}

//==============================================================================
bool GraphManager::canConnect (const juce::String& sourceUuid, int sourceChannel,
                               const juce::String& destUuid, int destChannel) const
{
    const auto nodesTree = rootState.getChildWithName (id::nodes);

    return sourceUuid != destUuid  // direkte Selbstverbindung wäre ein Graph-Zyklus
        && nodesTree.getChildWithProperty (id::nodeId, sourceUuid).isValid()
        && nodesTree.getChildWithProperty (id::nodeId, destUuid).isValid()
        && ! findConnectionTree (sourceUuid, sourceChannel, destUuid, destChannel).isValid();
}

void GraphManager::appendConnectionChild (const juce::String& sourceUuid, int sourceChannel,
                                          const juce::String& destUuid, int destChannel)
{
    juce::ValueTree connection (id::connection);
    connection.setProperty (id::sourceNodeId,  sourceUuid,    nullptr);
    connection.setProperty (id::sourceChannel, sourceChannel, nullptr);
    connection.setProperty (id::destNodeId,    destUuid,      nullptr);
    connection.setProperty (id::destChannel,   destChannel,   nullptr);

    rootState.getChildWithName (id::connections).appendChild (connection, &undoManager);
}

bool GraphManager::addConnection (const juce::String& sourceUuid, int sourceChannel,
                                  const juce::String& destUuid, int destChannel)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (! canConnect (sourceUuid, sourceChannel, destUuid, destChannel))
        return false;

    undoManager.beginNewTransaction ("Kabel verbinden");
    appendConnectionChild (sourceUuid, sourceChannel, destUuid, destChannel);
    return true;
}

bool GraphManager::addConnectionPair (const juce::String& sourceUuid, int sourceChannel,
                                      const juce::String& destUuid, int destChannel)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (! canConnect (sourceUuid, sourceChannel, destUuid, destChannel))
        return false;

    undoManager.beginNewTransaction ("Kabel verbinden");
    appendConnectionChild (sourceUuid, sourceChannel, destUuid, destChannel);

    // Zweites Kabel (Partner-Kanäle) nur, wenn das Ziel den Kanal trägt und
    // die Verbindung frei ist — sonst Mono-Fallback (Header-Doku). Ein Undo
    // entfernt beide Kabel (eine Transaktion, Batch-Coalescing 5.5).
    const auto destTree = rootState.getChildWithName (id::nodes)
                              .getChildWithProperty (id::nodeId, destUuid);
    const auto destChannels = (int) destTree.getProperty (id::numInputChannels, 0);

    if (destChannel + 1 < destChannels
        && canConnect (sourceUuid, sourceChannel + 1, destUuid, destChannel + 1))
        appendConnectionChild (sourceUuid, sourceChannel + 1, destUuid, destChannel + 1);

    return true;
}

bool GraphManager::removeConnection (const juce::String& sourceUuid, int sourceChannel,
                                     const juce::String& destUuid, int destChannel)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto connection = findConnectionTree (sourceUuid, sourceChannel, destUuid, destChannel);

    if (! connection.isValid())
        return false;

    undoManager.beginNewTransaction ("Kabel trennen");
    rootState.getChildWithName (id::connections).removeChild (connection, &undoManager);
    return true;
}

bool GraphManager::removeConnectionPair (const juce::String& sourceUuid, int sourceChannelA,
                                         const juce::String& destUuid, int destChannelA,
                                         int sourceChannelB, int destChannelB)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto first  = findConnectionTree (sourceUuid, sourceChannelA, destUuid, destChannelA);
    const auto second = findConnectionTree (sourceUuid, sourceChannelB, destUuid, destChannelB);

    if (! first.isValid() && ! second.isValid())
        return false;

    // Beide Kabel des Paars in EINER Transaktion — ein Undo stellt beide
    // wieder her (Batch-Coalescing 5.5)
    undoManager.beginNewTransaction ("Kabel trennen");
    auto connectionsTree = rootState.getChildWithName (id::connections);

    if (first.isValid())
        connectionsTree.removeChild (first, &undoManager);
    if (second.isValid())
        connectionsTree.removeChild (second, &undoManager);

    return true;
}

juce::ValueTree GraphManager::findConnectionTree (const juce::String& sourceUuid, int sourceChannel,
                                                  const juce::String& destUuid, int destChannel) const
{
    const auto connectionsTree = rootState.getChildWithName (id::connections);

    for (int i = 0; i < connectionsTree.getNumChildren(); ++i)
    {
        const auto connection = connectionsTree.getChild (i);

        if (connection.getProperty (id::sourceNodeId).toString() == sourceUuid
            && (int) connection.getProperty (id::sourceChannel) == sourceChannel
            && connection.getProperty (id::destNodeId).toString() == destUuid
            && (int) connection.getProperty (id::destChannel) == destChannel)
            return connection;
    }

    return {};
}

//==============================================================================
void GraphManager::snapshotAutoName (juce::ValueTree sendNodeTree, int destChannel)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto inputsTree = sendNodeTree.getChildWithName (id::inputs);

    // Eingang finden, der destChannel enthält, und seinen Start-Kanal (offset)
    int offset = 0;
    juce::ValueTree inputTree;

    for (int i = 0; i < inputsTree.getNumChildren(); ++i)
    {
        const auto in = inputsTree.getChild (i);
        const int width = in.getProperty (id::inputMode).toString() == LinkAudioSendModule::modeStereo ? 2 : 1;

        if (destChannel >= offset && destChannel < offset + width)
        {
            inputTree = in;
            break;
        }

        offset += width;
    }

    if (! inputTree.isValid())
        return;

    // Snapshot nur, wenn weder userName noch autoName gesetzt sind.
    if (inputTree.getProperty (id::inputUserName).toString().isNotEmpty()
        || inputTree.getProperty (id::inputAutoName).toString().isNotEmpty())
        return;

    // Quell-Label vom repräsentativen (linken) Kanal des Eingangs.
    const auto label = resolveSourceLabel (rootState,
                                           sendNodeTree.getProperty (id::nodeId).toString(),
                                           offset, channelNames);

    if (label.isNotEmpty())
        inputTree.setProperty (id::inputAutoName, label, nullptr);  // abgeleitet, non-undoable
}

bool GraphManager::refreshAutoNames (const juce::String& nodeUuid)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto nodeTree = rootState.getChildWithName (id::nodes)
                              .getChildWithProperty (id::nodeId, nodeUuid);

    if (! nodeTree.isValid() || factoryKeyOf (nodeTree) != LinkAudioSendModule::staticModuleId)
        return false;

    const auto inputsTree = nodeTree.getChildWithName (id::inputs);

    undoManager.beginNewTransaction ("Auto-Namen aktualisieren");

    int offset = 0;
    for (int i = 0; i < inputsTree.getNumChildren(); ++i)
    {
        auto in = inputsTree.getChild (i);
        const int width = in.getProperty (id::inputMode).toString() == LinkAudioSendModule::modeStereo ? 2 : 1;

        // autoName aus der aktuellen Quelle neu ziehen (userName bleibt).
        const auto label = resolveSourceLabel (rootState, nodeUuid, offset, channelNames);
        in.setProperty (id::inputAutoName, label, &undoManager);

        offset += width;
    }

    return true;
}

//==============================================================================
void GraphManager::registerExternalEndpoint (const juce::String& moduleId,
                                             juce::AudioProcessorGraph::NodeID graphNodeId)
{
    JUCE_ASSERT_MESSAGE_THREAD
    externalEndpoints[moduleId] = graphNodeId;
}

bool GraphManager::isExternalEndpoint (const juce::String& moduleId) const noexcept
{
    return externalEndpoints.contains (moduleId);
}

bool GraphManager::isExternalGraphNode (juce::AudioProcessorGraph::NodeID nodeId) const
{
    return std::any_of (externalEndpoints.begin(), externalEndpoints.end(),
                        [nodeId] (const auto& entry) { return entry.second == nodeId; });
}

void GraphManager::setClockBus (const ClockBus* bus) noexcept
{
    clockBus = bus;
}

void GraphManager::setLinkClock (LinkClock* clock) noexcept
{
    linkClock = clock;
}

void GraphManager::setCaptureService (CaptureService* service) noexcept
{
    captureService = service;
}

void GraphManager::setChannelNames (ChannelNames* names) noexcept
{
    channelNames = names;
}

void GraphManager::setModuleUiDefaults (ModuleUiDefaults* defaults) noexcept
{
    uiDefaults = defaults;
}

void GraphManager::setPageManager (PageManager* pages) noexcept
{
    pageManager = pages;
}

bool GraphManager::captureModuleUiDefaults (const juce::String& nodeUuid)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto nodeTree = rootState.getChildWithName (id::nodes)
                              .getChildWithProperty (id::nodeId, nodeUuid);

    if (uiDefaults == nullptr || ! nodeTree.isValid())
        return false;

    uiDefaults->captureFromNode (nodeTree);
    return true;
}

//==============================================================================
bool GraphManager::isTopologyContainer (const juce::ValueTree& tree) noexcept
{
    return tree.hasType (id::nodes) || tree.hasType (id::connections);
}

void GraphManager::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    // Phase 1 des zweiphasigen Deletes (5.3): moduleId wird JETZT gecacht,
    // nicht erst bei valueTreeChildRemoved — der künftige OscController
    // deregistriert seine OSC-Adressen darüber (7.1).
    if (property == id::nodeState && tree.hasType (id::node)
        && tree.getProperty (id::nodeState).toString() == toString (NodeState::deleting))
    {
        const auto nodeUuid = tree.getProperty (id::nodeId).toString();

        // Phase-1-Hook (7.2): Link-Audio-Sinks SOFORT zurückziehen — wie die
        // OSC-Deregistrierung nicht erst beim Subtree-Remove, sonst sehen
        // die Peers Zombie-Kanäle bis Phase 2.
        auto* deletingModule = getModuleFor (nodeUuid);
        if (auto* linkAudioClient = dynamic_cast<ILinkAudioClient*> (deletingModule))
            linkAudioClient->releaseSessionResources();

        // Phase-1-Hook Capture-Taps: Schreibpfad sofort trennen — laufendes
        // Material bleibt beim Service als "held" erhalten (5.3)
        if (auto* tapClient = dynamic_cast<ICaptureTapClient*> (deletingModule))
            tapClient->releaseCaptureResources();

        pendingDeletes[nodeUuid] = tree.getProperty (id::moduleId).toString();
        triggerAsyncUpdate();
        return;
    }

    // Hardware-I/O-Kanalzahl geändert (Gerätewechsel, ADR 009): das
    // Pass-Through-Modul braucht neue Busse → re-materialisieren. Hartes
    // removeNode ohne Fade — der Gerätewechsel startet Audio ohnehin neu.
    if ((property == id::numInputChannels || property == id::numOutputChannels)
        && tree.hasType (id::node) && isExternalEndpoint (factoryKeyOf (tree)))
    {
        const auto nodeUuid = tree.getProperty (id::nodeId).toString();

        if (const auto it = treeToGraphNode.find (nodeUuid); it != treeToGraphNode.end())
        {
            graph.removeNode (it->second);   // entfernt auch die Anker-Kabel
            treeToGraphNode.erase (nodeUuid);
        }

        markTopologyDirty();   // nächster Swap materialisiert mit neuen Bussen
        return;
    }

    // Rename der named_id (renameNode, auch via Undo): Kanal-Name eines
    // Link-Audio-Sinks folgt live (7.2). Noch nicht materialisierte Module
    // bekommen die aktuelle moduleId ohnehin bei der Materialisierung.
    if (property == id::moduleId && tree.hasType (id::node))
    {
        auto* renamedModule = getModuleFor (tree.getProperty (id::nodeId).toString());
        const auto newName = tree.getProperty (id::moduleId).toString();

        if (auto* linkAudioClient = dynamic_cast<ILinkAudioClient*> (renamedModule))
            linkAudioClient->moduleIdRenamed (newName);

        if (auto* tapClient = dynamic_cast<ICaptureTapClient*> (renamedModule))
            tapClient->captureModuleIdRenamed (newName);  // Spurnamen folgen live

        return;
    }

    // Send-Eingangs-Name geändert (userName/autoName, 7.2): der effektive
    // Kanal-Name folgt live zum Sink — KEIN Rebuild (fixe Kanalzahl).
    if ((property == id::inputUserName || property == id::inputAutoName)
        && tree.hasType (id::input))
    {
        const auto inputsTree = tree.getParent();
        const auto nodeTree   = inputsTree.getParent();

        if (nodeTree.hasType (id::node))
            if (auto* sendClient = dynamic_cast<ISendConfigClient*> (
                    getModuleFor (nodeTree.getProperty (id::nodeId).toString())))
                sendClient->inputNameChanged (
                    tree.getProperty (id::inputId).toString(),
                    LinkAudioSendModule::effectiveInputName (tree, inputsTree.indexOf (tree)));

        return;
    }

    // Link-Send-Tap des FX-Chassis (4.6): Node-Property → Modul, LIVE ohne
    // Rebuild (create/retire am laufenden System). Auch via Undo/Preset-Diff.
    if (property == id::linkSendEnabled && tree.hasType (id::node))
    {
        if (auto* processor = dynamic_cast<ProcessorModule*> (
                getModuleFor (tree.getProperty (id::nodeId).toString())))
            processor->setSendEnabled ((bool) tree.getProperty (id::linkSendEnabled, false));

        return;
    }

    // Receive-Kanal-Wunsch geändert (7.2): Namen → Modul, LIVE ohne Rebuild
    // (rebind am laufenden System). Auch via Undo/Preset-Diff.
    if ((property == id::targetPeer || property == id::targetChannel) && tree.hasType (id::node))
    {
        if (auto* receiveModule = dynamic_cast<LinkAudioReceiveModule*> (
                getModuleFor (tree.getProperty (id::nodeId).toString())))
            receiveModule->setTargetChannel (tree.getProperty (id::targetPeer).toString(),
                                             tree.getProperty (id::targetChannel).toString());

        return;
    }

    // Tree → Atomic: paramValue-Änderungen (UI-Slider, OSC-Nachzug, Undo,
    // Preset-Load) auf das Echtzeit-Target spiegeln — KEIN Rebuild.
    if (property == id::paramValue && tree.hasType (id::parameter))
    {
        syncParameterValue (tree);
        return;
    }

    // User-Regelbereich / Control-Link (Dev-Modus 4.6, auch via Undo/
    // Preset-Diff): LIVE ins Modul spiegeln — KEIN Rebuild.
    if ((property == id::paramUserMin || property == id::paramUserMax
         || property == id::paramLinkSource || property == id::paramLinkAmount
         || property == id::paramLinkCurve)
        && tree.hasType (id::parameter))
    {
        const auto nodeTree = tree.getParent().getParent();

        if (nodeTree.hasType (id::node))
            if (auto* processor = dynamic_cast<ProcessorModule*> (
                    getModuleFor (nodeTree.getProperty (id::nodeId).toString())))
            {
                if (property == id::paramUserMin || property == id::paramUserMax)
                    syncParameterUserRange (tree, *processor);
                else
                    syncParameterLink (tree, *processor);
            }

        return;
    }

    // Sonst bewusst keine Reaktion — Topologie ändert sich hier nicht,
    // ein Graph-Rebuild wäre falsch (6.1).
}

void GraphManager::syncParameterLink (const juce::ValueTree& parameterTree,
                                      ProcessorModule& processor)
{
    const auto paramId = parameterTree.getProperty (id::paramId).toString();

    processor.setParameterLink (
        paramId,
        parameterTree.getProperty (id::paramLinkSource).toString(),
        static_cast<float> ((double) parameterTree.getProperty (id::paramLinkAmount, 0.0)));

    processor.setParameterLinkCurve (
        paramId,
        ChassisSchema::parseLinkResponse (parameterTree.getProperty (id::paramLinkCurve).toString()));
}

void GraphManager::syncParameterUserRange (const juce::ValueTree& parameterTree,
                                           ProcessorModule& processor)
{
    // Effektiver User-Bereich: fehlende Properties fallen auf die Hard-Range
    processor.setParameterUserRange (
        parameterTree.getProperty (id::paramId).toString(),
        static_cast<float> ((double) parameterTree.getProperty (
            id::paramUserMin, parameterTree.getProperty (id::paramMin, 0.0))),
        static_cast<float> ((double) parameterTree.getProperty (
            id::paramUserMax, parameterTree.getProperty (id::paramMax, 1.0))));
}

void GraphManager::syncParameterValue (juce::ValueTree parameterTree)
{
    // Parameter → Parameters[] → Node (Schema 6.2)
    const auto nodeTree = parameterTree.getParent().getParent();

    if (! nodeTree.hasType (id::node))
        return;

    auto* module = getModuleFor (nodeTree.getProperty (id::nodeId).toString());

    if (module == nullptr)
        return;  // noch nicht materialisiert — addNewNodes() spiegelt initial

    const auto paramId = parameterTree.getProperty (id::paramId).toString();

    if (auto* target = module->getParameterTarget (paramId))
    {
        const auto minValue = static_cast<float> ((double) parameterTree.getProperty (id::paramMin, 0.0));
        const auto maxValue = static_cast<float> ((double) parameterTree.getProperty (id::paramMax, 1.0));
        const auto value    = static_cast<float> ((double) parameterTree.getProperty (id::paramValue, 0.0));

        auto effective = juce::jlimit (minValue, maxValue, value);

        // M5c: aktiven Macro-Modulations-Offset einkomponieren — der Tree
        // behält die BASIS (Fader/Preset/OSC-Send unberührt), nur das
        // Atomic trägt den Effektivwert (dokumentierte 6.1-Erweiterung).
        const auto modIt = paramModulations.find (
            { nodeTree.getProperty (id::nodeId).toString(), paramId });
        if (modIt != paramModulations.end())
        {
            const auto userMin = static_cast<float> ((double) parameterTree.getProperty (
                id::paramUserMin, parameterTree.getProperty (id::paramMin, 0.0)));
            const auto userMax = static_cast<float> ((double) parameterTree.getProperty (
                id::paramUserMax, parameterTree.getProperty (id::paramMax, 1.0)));

            effective = computeModulatedValue (effective, modIt->second,
                                               userMin, userMax, minValue, maxValue);
        }

        target->store (effective, std::memory_order_relaxed);
    }
}

juce::ValueTree GraphManager::parameterTreeFor (const juce::String& nodeUuid,
                                                const juce::String& paramId) const
{
    const auto nodeTree = rootState.getChildWithName (id::nodes)
                              .getChildWithProperty (id::nodeId, nodeUuid);
    if (! nodeTree.isValid())
        return {};

    return nodeTree.getChildWithName (id::parameters)
        .getChildWithProperty (id::paramId, paramId);
}

//==============================================================================
void GraphManager::setParamModulation (const ParamModKey& key, float offsetNorm)
{
    JUCE_ASSERT_MESSAGE_THREAD

    paramModulations[key] = juce::jlimit (-1.0f, 1.0f, offsetNorm);

    // Modul fehlt/nicht materialisiert → syncParameterValue ist No-op, der
    // gemerkte Offset greift beim nächsten Rebuild (addNewNodes spiegelt).
    if (auto parameterTree = parameterTreeFor (key.nodeUuid, key.paramId); parameterTree.isValid())
        syncParameterValue (parameterTree);
}

void GraphManager::clearParamModulation (const ParamModKey& key)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (paramModulations.erase (key) == 0)
        return;

    // Basiswert kehrt ins Atomic zurück.
    if (auto parameterTree = parameterTreeFor (key.nodeUuid, key.paramId); parameterTree.isValid())
        syncParameterValue (parameterTree);
}

std::optional<float> GraphManager::getParamModulationEffective (const juce::String& nodeUuid,
                                                                const juce::String& paramId) const
{
    const auto modIt = paramModulations.find ({ nodeUuid, paramId });
    if (modIt == paramModulations.end())
        return std::nullopt;

    const auto parameterTree = parameterTreeFor (nodeUuid, paramId);
    if (! parameterTree.isValid())
        return std::nullopt;

    const auto minValue = static_cast<float> ((double) parameterTree.getProperty (id::paramMin, 0.0));
    const auto maxValue = static_cast<float> ((double) parameterTree.getProperty (id::paramMax, 1.0));
    const auto base     = juce::jlimit (minValue, maxValue,
                                        static_cast<float> ((double) parameterTree.getProperty (id::paramValue, 0.0)));
    const auto userMin  = static_cast<float> ((double) parameterTree.getProperty (
        id::paramUserMin, parameterTree.getProperty (id::paramMin, 0.0)));
    const auto userMax  = static_cast<float> ((double) parameterTree.getProperty (
        id::paramUserMax, parameterTree.getProperty (id::paramMax, 1.0)));

    return computeModulatedValue (base, modIt->second, userMin, userMax, minValue, maxValue);
}

void GraphManager::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    // parent: Node/Connection in einen Container eingefügt.
    // child:  ganzer Container ersetzt (Preset-Load via
    //         copyPropertiesAndChildrenFrom hängt Nodes[]/Connections[]
    //         als Subtree an den Root — parent ist dann der Root).
    // Auto-Naming-Snapshot (7.2 Schritt 3): frisch gezogenes Kabel an einen
    // Send-Eingang → Quell-Name EINMAL übernehmen (kein Live-Follow, damit
    // Ableton-Routing stabil bleibt).
    if (child.hasType (id::connection))
    {
        const auto destUuid = child.getProperty (id::destNodeId).toString();
        const auto destNode = rootState.getChildWithName (id::nodes)
                                  .getChildWithProperty (id::nodeId, destUuid);

        if (destNode.isValid() && factoryKeyOf (destNode) == LinkAudioSendModule::staticModuleId)
            snapshotAutoName (destNode, static_cast<int> (child.getProperty (id::destChannel)));
    }

    if (isTopologyContainer (parent) || isTopologyContainer (child))
        markTopologyDirty();
}

void GraphManager::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (isTopologyContainer (parent) || isTopologyContainer (child))
        markTopologyDirty();
}

void GraphManager::valueTreeRedirected (juce::ValueTree&)
{
    // Kompletter Tree-Austausch → ein voller Rebuild.
    markTopologyDirty();
}

void GraphManager::markTopologyDirty()
{
    // ValueTree-Mutationen sind nur auf dem Message Thread erlaubt (CLAUDE.md 6)
    JUCE_ASSERT_MESSAGE_THREAD

    topologyDirty = true;
    ++pendingChangeCount;
    triggerAsyncUpdate();  // coalesct mehrfache Trigger bis zum nächsten Loop-Durchlauf
}

//==============================================================================
void GraphManager::handleAsyncUpdate()
{
    // Phase 2 ausstehender Deletes (5.3): entfernt freigegebene Subtrees
    // und markiert dadurch topologyDirty — der Swap unten nimmt sie mit.
    processPendingDeletes();

    if (swapPhase == SwapPhase::waitingForSilence)
    {
        // Schritt 3: kein Busy-Poll, kein Timer — Self-Re-Dispatch bis der
        // Audio Thread Stille meldet. Stoppt Audio währenddessen (Fader
        // unprepared), wird ohne Fade direkt geswappt.
        if (fader.isPrepared() && ! fader.isFadeOutComplete())
        {
            triggerAsyncUpdate();
            return;
        }

        performTopologySwap();  // Topologie-Swap auf Stille
        fader.beginFadeIn();    // Schritt 4
        swapPhase = SwapPhase::idle;
        return;
    }

    if (! topologyDirty)
        return;

    // Schritt 1 (Async Prepare): neue Module VOR dem Fade-Out instanziieren
    // und vorbereiten — speicherintensive Allokationen passieren hier,
    // nicht während der Stille.
    prepareNewModules();

    if (! fader.isPrepared())
    {
        // Audio läuft nicht — Fade wäre wirkungslos, direkt swappen.
        performTopologySwap();
        return;
    }

    // Schritt 2: Fade-Out anstoßen, die Graph-Topologie bleibt unverändert.
    // Änderungen, die während des Fade-Outs eintreffen, erhöhen nur das
    // Delta und landen im selben Swap (5.5).
    fader.beginFadeOut();
    swapPhase = SwapPhase::waitingForSilence;
    triggerAsyncUpdate();
}

//==============================================================================
void GraphManager::processPendingDeletes()
{
    if (pendingDeletes.empty())
        return;

    auto nodesTree       = rootState.getChildWithName (id::nodes);
    auto connectionsTree = rootState.getChildWithName (id::connections);

    for (auto it = pendingDeletes.begin(); it != pendingDeletes.end();)
    {
        const auto& nodeUuid = it->first;

        // Zombie-UI-Schutz: Phase 2 startet erst, wenn keine UI-Component
        // mehr eine Referenz auf diesen Subtree hält (5.3). Die Freigabe
        // stößt via NodeUiRegistry-Callback den nächsten Durchlauf an.
        if (uiRegistry.getRefCount (nodeUuid) > 0)
        {
            ++it;
            continue;
        }

        if (auto nodeTree = nodesTree.getChildWithProperty (id::nodeId, nodeUuid); nodeTree.isValid())
        {
            // Node und seine Kabel in EINER UndoManager-Transaktion —
            // ein Undo stellt alles wieder her und löst einen einzigen
            // gemeinsamen Swap aus (5.5)
            undoManager.beginNewTransaction ("Modul löschen");

            for (int i = connectionsTree.getNumChildren(); --i >= 0;)
            {
                const auto connection = connectionsTree.getChild (i);

                if (connection.getProperty (id::sourceNodeId).toString() == nodeUuid
                    || connection.getProperty (id::destNodeId).toString() == nodeUuid)
                    connectionsTree.removeChild (i, &undoManager);
            }

            nodesTree.removeChild (nodeTree, &undoManager);
        }

        it = pendingDeletes.erase (it);
    }
}

//==============================================================================
void GraphManager::prepareNewModules()
{
    const auto nodesTree = rootState.getChildWithName (id::nodes);

    for (int i = 0; i < nodesTree.getNumChildren(); ++i)
    {
        auto nodeTree = nodesTree.getChild (i);
        const auto nodeUuid = nodeTree.getProperty (id::nodeId).toString();

        normalizeNode (nodeTree);  // Migration vor jeder Auswertung

        if (nodeUuid.isEmpty()
            || treeToGraphNode.contains (nodeUuid)
            || preparedModules.contains (nodeUuid)
            || nodeTree.getProperty (id::nodeError).toString().isNotEmpty())
            continue;

        if (auto module = materializeModule (nodeTree))
            preparedModules[nodeUuid] = std::move (module);
    }
}

std::unique_ptr<ConduitModule> GraphManager::materializeModule (juce::ValueTree nodeTree)
{
    const auto factoryKey = factoryKeyOf (nodeTree);
    auto module = factory.create (factoryKey);

    if (module == nullptr)
    {
        nodeTree.setProperty (id::nodeError, "Unbekanntes Modul: " + factoryKey, nullptr);
        return nullptr;
    }

    // Link-Audio-Kontext VOR prepareForGraph (7.2): der Sink entsteht in
    // prepareToPlay und braucht Clock + moduleId (Kanal-Name = {moduleId}/…)
    if (auto* linkAudioClient = dynamic_cast<ILinkAudioClient*> (module.get()))
        linkAudioClient->setLinkAudioContext (linkClock,
            nodeTree.getProperty (id::moduleId).toString());

    // FX-Chassis (4.6): persistierten Send-Zustand VOR prepareForGraph
    // setzen — der Tap entsteht dann in prepareToPlay (Preset-Load-Pfad).
    // Dazu die User-Regelbereiche (Dev-Modus) aus dem Tree in die
    // Modul-Atomics spiegeln (Wirkbereich der CV-Modulation).
    if (auto* processor = dynamic_cast<ProcessorModule*> (module.get()))
    {
        processor->setSendEnabled ((bool) nodeTree.getProperty (id::linkSendEnabled, false));

        const auto params = nodeTree.getChildWithName (id::parameters);

        for (int i = 0; i < params.getNumChildren(); ++i)
        {
            const auto param = params.getChild (i);

            if (ChassisSchema::roleOf (param) != juce::String (ChassisSchema::roleDsp))
                continue;

            if (param.hasProperty (id::paramUserMin) || param.hasProperty (id::paramUserMax))
                syncParameterUserRange (param, *processor);

            if (param.hasProperty (id::paramLinkSource) || param.hasProperty (id::paramLinkCurve))
                syncParameterLink (param, *processor);
        }
    }

    // Send-Kanal-Layout VOR prepareForGraph (7.2): setzt die Bus-Kanalzahl
    // (fixe Eingangszahl) und die Eingangs-Struktur, aus der prepareToPlay
    // die Sinks baut.
    if (auto* sendClient = dynamic_cast<ISendConfigClient*> (module.get()))
        sendClient->applySendConfig (LinkAudioSendModule::readInputConfig (nodeTree));

    // Hardware-I/O-Endpunkt (ADR 009): Kanalzahl aus dem Tree VOR
    // prepareForGraph — bestimmt die Pass-Through-Busse. Port-Sicht 6.2:
    // input-Endpunkte tragen die Kanäle als AUSGANGS-Ports, output als
    // Eingangs-Ports.
    if (auto* endpoint = dynamic_cast<IExternalAudioEndpoint*> (module.get()))
        endpoint->setEndpointChannels ((int) nodeTree.getProperty (
            endpoint->isInputEndpoint() ? id::numOutputChannels : id::numInputChannels, 2));

    // Receive-Kanal-Wunsch (7.2): persistierte Namen ans Modul spiegeln —
    // rebind() matcht sie gegen die Session (Preset-Load-Pfad).
    if (auto* receiveModule = dynamic_cast<LinkAudioReceiveModule*> (module.get()))
        receiveModule->setTargetChannel (nodeTree.getProperty (id::targetPeer).toString(),
                                         nodeTree.getProperty (id::targetChannel).toString());

    // Capture-Kontext VOR prepareForGraph: die Kanal-Registrierung passiert
    // dort und braucht Service + moduleId (Spurname == moduleId)
    if (auto* tapClient = dynamic_cast<ICaptureTapClient*> (module.get()))
        tapClient->setCaptureTapContext (captureService,
            nodeTree.getProperty (id::moduleId).toString());

    // Läuft Audio noch nicht, werden die Latenz-Ziele aus CLAUDE.md 3.2
    // angenommen — graph.prepareToPlay() re-prepariert später mit Ist-Werten.
    const auto sampleRate = graph.getSampleRate() > 0.0 ? graph.getSampleRate() : 48000.0;
    const auto blockSize  = graph.getBlockSize()  > 0   ? graph.getBlockSize()  : 32;

    if (const auto result = module->prepareForGraph (sampleRate, blockSize); result.failed())
    {
        // Kein Crash, kein Retry-Loop — UI zeigt den Fehlerzustand (5.2 Schritt 1)
        nodeTree.setProperty (id::nodeError, result.getErrorMessage(), nullptr);
        return nullptr;
    }

    // Takt-Injektion VOR der Graph-Aufnahme (IClockSlave, 4.2) — danach
    // läuft processBlock und der Pointer darf sich nicht mehr ändern
    if (auto* clockSlave = dynamic_cast<IClockSlave*> (module.get()))
        clockSlave->setClockBus (clockBus);

    // Seed-Injektion (IStochastic, 4.2): deterministisch aus der nodeUuid —
    // Zufalls-Patterns sind damit pro Node reproduzierbar
    if (auto* stochastic = dynamic_cast<IStochastic*> (module.get()))
        stochastic->setRandomSeed (static_cast<std::uint64_t> (
            nodeTree.getProperty (id::nodeId).toString().hashCode64()));

    return module;
}

//==============================================================================
void GraphManager::performTopologySwap()
{
    const auto coalescedChanges = pendingChangeCount;
    topologyDirty = false;
    pendingChangeCount = 0;
    ++rebuildCount;

    removeVanishedNodes();
    addNewNodes();
    syncConnections();

    // Übrig gebliebene Instanzen (Tree-Node wurde während des Fade-Outs
    // wieder entfernt) verwerfen
    preparedModules.clear();

    juce::Logger::writeToLog ("GraphManager: Graph-Swap #" + juce::String (rebuildCount)
                              + " (" + juce::String (coalescedChanges) + " Änderungen coalesced, "
                              + juce::String (graph.getNumNodes()) + " Graph-Nodes)");
}

void GraphManager::removeVanishedNodes()
{
    const auto nodesTree = rootState.getChildWithName (id::nodes);

    for (auto it = treeToGraphNode.begin(); it != treeToGraphNode.end();)
    {
        if (nodesTree.getChildWithProperty (id::nodeId, it->first).isValid())
        {
            ++it;
            continue;
        }

        // Externe Endpunkte (I/O des EngineProcessor) gehören nicht uns —
        // nur das Mapping lösen, den Graph-Node niemals zerstören
        if (! isExternalGraphNode (it->second))
            graph.removeNode (it->second);  // entfernt auch alle Kabel dieses Nodes

        it = treeToGraphNode.erase (it);
    }
}

void GraphManager::addNewNodes()
{
    const auto nodesTree = rootState.getChildWithName (id::nodes);

    for (int i = 0; i < nodesTree.getNumChildren(); ++i)
    {
        auto nodeTree = nodesTree.getChild (i);
        const auto nodeUuid = nodeTree.getProperty (id::nodeId).toString();

        normalizeNode (nodeTree);

        if (nodeUuid.isEmpty()
            || treeToGraphNode.contains (nodeUuid)
            || nodeTree.getProperty (id::nodeError).toString().isNotEmpty())
            continue;

        std::unique_ptr<ConduitModule> module;

        if (const auto it = preparedModules.find (nodeUuid); it != preparedModules.end())
        {
            module = std::move (it->second);  // in Schritt 1 vorbereitet
            preparedModules.erase (it);
        }
        else
        {
            // Node kam erst während des Fade-Outs hinzu — jetzt vorbereiten
            module = materializeModule (nodeTree);
        }

        if (module == nullptr)
            continue;

        if (const auto graphNode = graph.addNode (std::move (module)))
        {
            treeToGraphNode[nodeUuid] = graphNode->nodeID;

            // Hardware-Anker-Kabel (ADR 009): I/O-Endpunkt-Module implizit
            // mit dem AudioGraphIOProcessor verbinden — kein Patch-Zustand,
            // syncConnections rührt sie nicht an (eine Seite unmanaged)
            if (auto* endpoint = dynamic_cast<IExternalAudioEndpoint*> (graphNode->getProcessor()))
                connectEndpointAnchor (graphNode->nodeID, *endpoint);

            // Lifecycle-Status zurücksetzen — relevant nach Undo eines
            // Deletes: der Subtree wurde mit nodeState == Deleting restauriert
            nodeTree.setProperty (id::nodeState, toString (NodeState::active), nullptr);

            // Persistierte Parameter auf die frischen Atomic-Targets spiegeln
            // (Preset-Load, Undo-Restore — Tree ist Single Source of Truth)
            const auto parameters = nodeTree.getChildWithName (id::parameters);

            for (int p = 0; p < parameters.getNumChildren(); ++p)
                syncParameterValue (parameters.getChild (p));
        }
    }
}

void GraphManager::syncConnections()
{
    // Soll-Menge aus dem Tree (Schema 6.2). Kabel mit fehlendem Endpunkt
    // (z.B. Node mit nodeError) bleiben außen vor — sie werden beim
    // nächsten Swap erneut geprüft.
    std::vector<juce::AudioProcessorGraph::Connection> desired;
    const auto connectionsTree = rootState.getChildWithName (id::connections);

    for (int i = 0; i < connectionsTree.getNumChildren(); ++i)
    {
        const auto connection = connectionsTree.getChild (i);
        const auto source = treeToGraphNode.find (connection.getProperty (id::sourceNodeId).toString());
        const auto dest   = treeToGraphNode.find (connection.getProperty (id::destNodeId).toString());

        if (source == treeToGraphNode.end() || dest == treeToGraphNode.end())
            continue;

        desired.push_back ({ { source->second, (int) connection.getProperty (id::sourceChannel) },
                             { dest->second,   (int) connection.getProperty (id::destChannel) } });
    }

    // Ist-Zustand abgleichen: nur Kabel zwischen tree-verwalteten Nodes —
    // die I/O-Nodes des EngineProcessor bleiben unangetastet.
    for (const auto& existing : graph.getConnections())
    {
        if (! isManagedGraphNode (existing.source.nodeID) || ! isManagedGraphNode (existing.destination.nodeID))
            continue;

        if (std::find (desired.begin(), desired.end(), existing) == desired.end())
            graph.removeConnection (existing);
    }

    for (const auto& wanted : desired)
        if (! graph.isConnected (wanted) && ! graph.addConnection (wanted))
            juce::Logger::writeToLog ("GraphManager: ungültige Verbindung verworfen (Kanal außerhalb des Busses?)");
}

bool GraphManager::isManagedGraphNode (juce::AudioProcessorGraph::NodeID nodeId) const
{
    return std::any_of (treeToGraphNode.begin(), treeToGraphNode.end(),
                        [nodeId] (const auto& entry) { return entry.second == nodeId; });
}

void GraphManager::connectEndpointAnchor (juce::AudioProcessorGraph::NodeID proxyNodeId,
                                          IExternalAudioEndpoint& endpoint)
{
    // Hardware-Anker (die AudioGraphIOProcessor des EngineProcessor) —
    // ohne Registrierung (Tests) bleibt der Proxy einfach unverbunden
    const auto anchorIt = externalEndpoints.find (
        endpoint.isInputEndpoint() ? audioInputModuleId : audioOutputModuleId);

    if (anchorIt == externalEndpoints.end())
        return;

    for (int channel = 0; channel < endpoint.getEndpointChannels(); ++channel)
    {
        const auto connection = endpoint.isInputEndpoint()
            ? juce::AudioProcessorGraph::Connection { { anchorIt->second, channel },
                                                      { proxyNodeId,      channel } }
            : juce::AudioProcessorGraph::Connection { { proxyNodeId,      channel },
                                                      { anchorIt->second, channel } };

        // Kanäle jenseits der Anker-Busse lehnt der Graph ab — kein Fehler
        // (Hardware mit weniger Kanälen als der Tree behauptet)
        graph.addConnection (connection);
    }
}

} // namespace conduit
