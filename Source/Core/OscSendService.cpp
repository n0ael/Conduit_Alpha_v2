#include "OscSendService.h"

#include <set>

#include "Modules/ConduitModule.h"
#include "OscAddress.h"

namespace conduit
{

//==============================================================================
namespace
{
    class UdpOscSink final : public IOscSink
    {
    public:
        bool connect (const juce::String& host, int port) override
        {
            sender.disconnect();
            return sender.connect (host, port);
        }

        void disconnect() override { sender.disconnect(); }

        bool sendBundle (const juce::OSCBundle& bundle) override
        {
            return sender.send (bundle);
        }

    private:
        juce::OSCSender sender;
    };
} // namespace

std::unique_ptr<IOscSink> makeUdpOscSink()
{
    return std::make_unique<UdpOscSink>();
}

//==============================================================================
OscSendService::OscSendService (juce::ValueTree rootTree,
                                OscSendSettings& settingsToUse,
                                std::unique_ptr<IOscSink> sinkToUse)
    : rootState (std::move (rootTree)),
      settings (settingsToUse),
      sink (sinkToUse != nullptr ? std::move (sinkToUse) : makeUdpOscSink())
{
    settings.addChangeListener (this);
    applySettings();
}

OscSendService::~OscSendService()
{
    settings.removeChangeListener (this);
    stopTimer();
    sink->disconnect();
}

//==============================================================================
void OscSendService::noteRemoteValue (const juce::String& nodeUuid,
                                      const juce::String& parameterId,
                                      float value)
{
    JUCE_ASSERT_MESSAGE_THREAD
    lastSent[{ nodeUuid, parameterId }] = value;
}

void OscSendService::sendFullDump()
{
    JUCE_ASSERT_MESSAGE_THREAD
    sendDiff (true);
}

void OscSendService::sendNodeValues (const juce::String& nodeUuid)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (nodeUuid.isNotEmpty())
        sendDiff (true, nodeUuid);
}

void OscSendService::flushPendingSend()
{
    JUCE_ASSERT_MESSAGE_THREAD
    sendDiff (false);
}

//==============================================================================
void OscSendService::timerCallback()
{
    sendDiff (false);
}

void OscSendService::changeListenerCallback (juce::ChangeBroadcaster*)
{
    applySettings();
}

void OscSendService::applySettings()
{
    JUCE_ASSERT_MESSAGE_THREAD

    stopTimer();
    sink->disconnect();
    connected = false;

    // Frischer Cache bei jeder Zieländerung — der erste Tick nach dem
    // (Re-)Connect ist damit ein impliziter Voll-Sync (7.3)
    lastSent.clear();

    if (! settings.isEnabled())
        return;

    connected = sink->connect (settings.getHost(), settings.getPort());

    if (connected)
        startTimerHz (ticksPerSecond);
}

//==============================================================================
void OscSendService::sendDiff (bool force, const juce::String& onlyNodeUuid)
{
    if (! connected)
        return;

    const bool fullWalk = onlyNodeUuid.isEmpty();

    juce::OSCBundle bundle;
    std::set<Key> seen;  // Pruning-Basis (nur volle Walks)

    const auto nodesTree = rootState.getChildWithName (id::nodes);

    for (int i = 0; i < nodesTree.getNumChildren(); ++i)
    {
        const auto nodeTree = nodesTree.getChild (i);
        const auto nodeUuid = nodeTree.getProperty (id::nodeId).toString();

        if (nodeUuid.isEmpty()
            || nodeTree.getProperty (id::moduleId).toString().isEmpty())
            continue;

        if (! fullWalk && nodeUuid != onlyNodeUuid)
            continue;

        // Deleting-Nodes sind raus — wie in der Receive-Registry (5.3 / 7.1)
        if (nodeTree.getProperty (id::nodeState).toString() == toString (NodeState::deleting))
            continue;

        const auto parameters = nodeTree.getChildWithName (id::parameters);

        for (int p = 0; p < parameters.getNumChildren(); ++p)
        {
            const auto parameter   = parameters.getChild (p);
            const auto parameterId = parameter.getProperty (id::paramId).toString();

            if (parameterId.isEmpty())
                continue;

            // var speichert double — beidseitig über float vergleichen,
            // sonst diffed jeder Tick denselben Wert erneut (Dauersenden)
            const auto value = static_cast<float> (
                (double) parameter.getProperty (id::paramValue, 0.0));

            const Key key { nodeUuid, parameterId };
            seen.insert (key);

            if (! force)
            {
                const auto it = lastSent.find (key);

                if (it != lastSent.end() && juce::exactlyEqual (it->second, value))
                    continue;
            }

            lastSent[key] = value;
            bundle.addElement (juce::OSCMessage (
                juce::OSCAddressPattern (osc::parameterAddress (nodeTree, parameterId)),
                value));

            // Chunking: UDP-Paketgrenze — volle Bundles sofort raus
            if (bundle.size() >= maxMessagesPerBundle)
            {
                sink->sendBundle (bundle);
                bundle = juce::OSCBundle();
            }
        }
    }

    if (bundle.size() > 0)
        sink->sendBundle (bundle);

    if (fullWalk)
    {
        // Cache-Pruning: Einträge verschwundener Nodes/Parameter entfernen
        std::erase_if (lastSent, [&seen] (const auto& entry)
        {
            return seen.find (entry.first) == seen.end();
        });
    }
}

} // namespace conduit
