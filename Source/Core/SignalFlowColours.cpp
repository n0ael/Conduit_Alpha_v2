#include "SignalFlowColours.h"

#include <set>

#include "ChannelNames.h"
#include "GraphManager.h"
#include "Modules/ConduitModule.h"

namespace conduit::flow_colours
{

namespace
{
    juce::uint32 computeEffectiveRgb (const juce::ValueTree& rootTree,
                                      const ChannelNames* channelNames,
                                      ColourMap& colours,
                                      const juce::String& nodeUuid,
                                      std::set<juce::String>& visiting);

    /** Quellfarbe eines Kanals für die Vererbung: audio_in → ChannelNames
        pro Kanal, sonst effektive Farbe des Quellmoduls. */
    juce::uint32 sourceChannelRgb (const juce::ValueTree& rootTree,
                                   const ChannelNames* channelNames,
                                   ColourMap& colours,
                                   const juce::String& sourceUuid,
                                   int channel,
                                   std::set<juce::String>& visiting)
    {
        const auto node = rootTree.getChildWithName (id::nodes)
                              .getChildWithProperty (id::nodeId, sourceUuid);
        if (! node.isValid())
            return 0;

        if (channelNames != nullptr && GraphManager::factoryKeyOf (node) == audioInputModuleId)
            return inputChannelRgb (channelNames, channel);

        return computeEffectiveRgb (rootTree, channelNames, colours, sourceUuid, visiting);
    }

    juce::uint32 computeEffectiveRgb (const juce::ValueTree& rootTree,
                                      const ChannelNames* channelNames,
                                      ColourMap& colours,
                                      const juce::String& nodeUuid,
                                      std::set<juce::String>& visiting)
    {
        if (const auto it = colours.find (nodeUuid); it != colours.end())
            return it->second;  // memoisiert

        const auto node = rootTree.getChildWithName (id::nodes)
                              .getChildWithProperty (id::nodeId, nodeUuid);
        if (! node.isValid())
            return 0;

        // audio_in ist reine Quelle (Farbe pro Kanal) — kein Einzel-Node-Wert
        if (GraphManager::factoryKeyOf (node) == audioInputModuleId)
            return 0;

        // Explizite Node-Farbe gewinnt IMMER (bewusstes Label)
        if (const auto explicitRgb = (juce::uint32) (int) node.getProperty (id::nodeColour, 0);
            explicitRgb != 0)
        {
            colours[nodeUuid] = explicitRgb;
            return explicitRgb;
        }

        if (visiting.count (nodeUuid) > 0)
            return 0;  // Zyklus — Zweig nicht weiter verfolgen (nicht cachen)
        visiting.insert (nodeUuid);

        // Gemischte Farbe aller eingehenden Kabel (0/keine übersprungen)
        std::vector<juce::uint32> incoming;
        const auto connections = rootTree.getChildWithName (id::connections);
        for (int i = 0; i < connections.getNumChildren(); ++i)
        {
            const auto conn = connections.getChild (i);
            if (conn.getProperty (id::destNodeId).toString() != nodeUuid)
                continue;

            const auto srcUuid = conn.getProperty (id::sourceNodeId).toString();
            const auto srcCh   = (int) conn.getProperty (id::sourceChannel);

            if (const auto rgb = sourceChannelRgb (rootTree, channelNames, colours,
                                                   srcUuid, srcCh, visiting);
                rgb != 0)
                incoming.push_back (rgb);
        }

        visiting.erase (nodeUuid);

        const auto result = blendRgb (incoming);
        colours[nodeUuid] = result;
        return result;
    }
} // namespace

//==============================================================================
ColourMap computeAll (const juce::ValueTree& rootTree, const ChannelNames* channelNames)
{
    ColourMap colours;

    const auto nodes = rootTree.getChildWithName (id::nodes);
    for (int i = 0; i < nodes.getNumChildren(); ++i)
    {
        std::set<juce::String> visiting;
        computeEffectiveRgb (rootTree, channelNames, colours,
                             nodes.getChild (i).getProperty (id::nodeId).toString(), visiting);
    }

    return colours;
}

juce::uint32 lookupSource (const juce::ValueTree& rootTree, const ColourMap& colours,
                           const ChannelNames* channelNames,
                           const juce::String& sourceUuid, int sourceChannel)
{
    const auto node = rootTree.getChildWithName (id::nodes)
                          .getChildWithProperty (id::nodeId, sourceUuid);
    if (! node.isValid())
        return 0;

    // audio_in: Farbe pro Kanal (ChannelNames)
    if (channelNames != nullptr && GraphManager::factoryKeyOf (node) == audioInputModuleId)
        return inputChannelRgb (channelNames, sourceChannel);

    if (const auto it = colours.find (sourceUuid); it != colours.end())
        return it->second;

    return 0;
}

juce::uint32 inputChannelRgb (const ChannelNames* channelNames, int channel)
{
    if (channelNames == nullptr)
        return 0;

    using D = ChannelNames::Direction;

    // Partner-Kanal eines Paars (Vorgänger ist Pair-Start) → Farbe des Ankers
    auto anchor = channel;
    if (channel > 0
        && ! channelNames->isPortPairStart (D::input, channel)
        && channelNames->isPortPairStart (D::input, channel - 1))
        anchor = channel - 1;

    return channelNames->getColour (D::input, anchor);
}

juce::uint32 resolveDestSourceRgb (const juce::ValueTree& rootTree,
                                   const ChannelNames* channelNames,
                                   const juce::String& destNodeUuid, int destChannel)
{
    const auto connections = rootTree.getChildWithName (id::connections);

    for (int i = 0; i < connections.getNumChildren(); ++i)
    {
        const auto conn = connections.getChild (i);
        if (conn.getProperty (id::destNodeId).toString() != destNodeUuid
            || (int) conn.getProperty (id::destChannel) != destChannel)
            continue;

        ColourMap colours;
        std::set<juce::String> visiting;
        const auto sourceUuid = conn.getProperty (id::sourceNodeId).toString();
        return sourceChannelRgb (rootTree, channelNames, colours, sourceUuid,
                                 (int) conn.getProperty (id::sourceChannel), visiting);
    }

    return 0;
}

juce::uint32 blendRgb (const std::vector<juce::uint32>& colours)
{
    if (colours.empty())
        return 0;

    juce::uint64 r = 0, g = 0, b = 0;
    for (const auto c : colours)
    {
        r += (c >> 16) & 0xffu;
        g += (c >> 8)  & 0xffu;
        b += c         & 0xffu;
    }

    const auto n = (juce::uint64) colours.size();
    const auto rgb = (juce::uint32) (((r / n) << 16) | ((g / n) << 8) | (b / n));
    return rgb == 0 ? 0x010101u : rgb;  // 0 ist der „keine"-Sentinel
}

} // namespace conduit::flow_colours
