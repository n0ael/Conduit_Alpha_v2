#include "SourceNameResolver.h"

#include <limits>
#include <set>

#include "ChannelNames.h"
#include "Modules/ConduitModule.h"

namespace conduit
{

juce::String resolveSourceLabel (const juce::ValueTree& rootTree,
                                 const juce::String& destNodeUuid,
                                 int destChannel,
                                 const ChannelNames* channelNames)
{
    const auto connections = rootTree.getChildWithName (id::connections);

    // Rückwärts-Auflösung dest → source (Schema 6.2)
    juce::ValueTree connection;
    for (int i = 0; i < connections.getNumChildren(); ++i)
    {
        const auto candidate = connections.getChild (i);
        if (candidate.getProperty (id::destNodeId).toString() == destNodeUuid
            && static_cast<int> (candidate.getProperty (id::destChannel)) == destChannel)
        {
            connection = candidate;
            break;
        }
    }

    if (! connection.isValid())
        return {};

    const auto sourceUuid    = connection.getProperty (id::sourceNodeId).toString();
    const int  sourceChannel = static_cast<int> (connection.getProperty (id::sourceChannel));

    const auto sourceNode = rootTree.getChildWithName (id::nodes)
                                .getChildWithProperty (id::nodeId, sourceUuid);
    if (! sourceNode.isValid())
        return {};

    const auto factoryKey = sourceNode.hasProperty (id::factoryId)
                              ? sourceNode.getProperty (id::factoryId).toString()
                              : sourceNode.getProperty (id::moduleId).toString();

    // Quelle ist der Hardware-Eingangs-Endpunkt → Kanal-Label aus ChannelNames
    if (factoryKey == audioInputModuleId)
        return channelNames != nullptr
                 ? channelNames->getLabel (ChannelNames::Direction::input, sourceChannel)
                 : ChannelNames::defaultLabel (ChannelNames::Direction::input, sourceChannel);

    // Reguläres Modul: moduleId (+ Kanal-Suffix bei Multi-Output)
    auto label = sourceNode.getProperty (id::moduleId).toString();
    if (static_cast<int> (sourceNode.getProperty (id::numOutputChannels, 0)) > 1)
        label << ":" << juce::String (sourceChannel + 1);

    return label;
}

juce::String resolveSourceChainLabel (const juce::ValueTree& rootTree,
                                      const juce::String& destNodeUuid,
                                      int destChannel,
                                      const ChannelNames* channelNames)
{
    const auto connections = rootTree.getChildWithName (id::connections);
    const auto nodes = rootTree.getChildWithName (id::nodes);

    // Erster Schritt: exakter Ziel-Kanal; weiter rückwärts folgt jede
    // Station ihrem ERSTEN verbundenen Eingang (kleinster destChannel)
    const auto findIncoming = [&] (const juce::String& uuid, int channel,
                                   bool exactChannel) -> juce::ValueTree
    {
        juce::ValueTree best;
        auto bestChannel = std::numeric_limits<int>::max();

        for (int i = 0; i < connections.getNumChildren(); ++i)
        {
            const auto candidate = connections.getChild (i);
            if (candidate.getProperty (id::destNodeId).toString() != uuid)
                continue;

            const auto dc = static_cast<int> (candidate.getProperty (id::destChannel));
            if (exactChannel)
            {
                if (dc == channel)
                    return candidate;
            }
            else if (dc < bestChannel)
            {
                best = candidate;
                bestChannel = dc;
            }
        }

        return best;
    };

    juce::StringArray stations;
    std::set<juce::String> visited;

    auto uuid = destNodeUuid;
    auto channel = destChannel;
    auto exactChannel = true;

    for (;;)
    {
        const auto connection = findIncoming (uuid, channel, exactChannel);
        if (! connection.isValid())
            break;  // stations leer → Ziel unverkabelt; sonst: Wurzel-Modul erreicht

        const auto sourceUuid = connection.getProperty (id::sourceNodeId).toString();
        const auto sourceNode = nodes.getChildWithProperty (id::nodeId, sourceUuid);
        if (! sourceNode.isValid())
            break;

        const auto factoryKey = sourceNode.hasProperty (id::factoryId)
                                  ? sourceNode.getProperty (id::factoryId).toString()
                                  : sourceNode.getProperty (id::moduleId).toString();

        // Wurzel: Interface-Eingang → Kanal-Label ganz nach vorn
        if (factoryKey == audioInputModuleId)
        {
            const auto sourceChannel = static_cast<int> (connection.getProperty (id::sourceChannel));
            stations.insert (0, channelNames != nullptr
                ? channelNames->getLabel (ChannelNames::Direction::input, sourceChannel)
                : ChannelNames::defaultLabel (ChannelNames::Direction::input, sourceChannel));
            break;
        }

        if (! visited.insert (sourceUuid).second)
            break;  // Zyklus — Kette hier kappen

        stations.insert (0, sourceNode.getProperty (id::moduleId).toString());

        uuid = sourceUuid;
        channel = 0;
        exactChannel = false;
    }

    return stations.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 "));
}

} // namespace conduit
