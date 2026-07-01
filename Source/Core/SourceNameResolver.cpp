#include "SourceNameResolver.h"

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

} // namespace conduit
