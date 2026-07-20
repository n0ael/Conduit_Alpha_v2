#include <catch2/catch_test_macros.hpp>

#include "Core/ChannelNames.h"
#include "Core/SourceNameResolver.h"
#include "Modules/ConduitModule.h"
#include "Modules/LinkAudioSendModule.h"

namespace
{

juce::ValueTree makeRoot()
{
    juce::ValueTree root (conduit::id::root);
    root.appendChild (juce::ValueTree (conduit::id::nodes),       nullptr);
    root.appendChild (juce::ValueTree (conduit::id::connections), nullptr);
    return root;
}

juce::ValueTree addNode (juce::ValueTree& root, const juce::String& uuid,
                         const juce::String& factoryId, const juce::String& moduleId,
                         int numOut)
{
    juce::ValueTree node (conduit::id::node);
    node.setProperty (conduit::id::nodeId,            uuid,      nullptr);
    node.setProperty (conduit::id::factoryId,         factoryId, nullptr);
    node.setProperty (conduit::id::moduleId,          moduleId,  nullptr);
    node.setProperty (conduit::id::numOutputChannels, numOut,    nullptr);
    root.getChildWithName (conduit::id::nodes).appendChild (node, nullptr);
    return node;
}

void addConnection (juce::ValueTree& root, const juce::String& srcUuid, int srcCh,
                    const juce::String& dstUuid, int dstCh)
{
    juce::ValueTree c (conduit::id::connection);
    c.setProperty (conduit::id::sourceNodeId,  srcUuid, nullptr);
    c.setProperty (conduit::id::sourceChannel, srcCh,   nullptr);
    c.setProperty (conduit::id::destNodeId,    dstUuid, nullptr);
    c.setProperty (conduit::id::destChannel,   dstCh,   nullptr);
    root.getChildWithName (conduit::id::connections).appendChild (c, nullptr);
}

} // namespace

//==============================================================================
TEST_CASE ("resolveSourceLabel: keine Verbindung → leer", "[autoname]")
{
    auto root = makeRoot();
    addNode (root, "send", conduit::LinkAudioSendModule::staticModuleId, "send1", 0);

    REQUIRE (conduit::resolveSourceLabel (root, "send", 0, nullptr).isEmpty());
}

//==============================================================================
TEST_CASE ("resolveSourceLabel: audio_input-Quelle → Kanal-Label (Fallback In N)", "[autoname]")
{
    auto root = makeRoot();
    addNode (root, "hw",   conduit::audioInputModuleId, conduit::audioInputModuleId, 2);
    addNode (root, "send", conduit::LinkAudioSendModule::staticModuleId, "send1", 0);

    // audio_input-Kanal 2 (0-basiert) an Send-Eingang 0
    addConnection (root, "hw", 2, "send", 0);

    // ChannelNames == nullptr → defaultLabel "In 3" (1-basiert)
    REQUIRE (conduit::resolveSourceLabel (root, "send", 0, nullptr)
             == conduit::ChannelNames::defaultLabel (conduit::ChannelNames::Direction::input, 2));
}

//==============================================================================
TEST_CASE ("resolveSourceLabel: Modul-Quelle → moduleId, Multi-Out mit Suffix", "[autoname]")
{
    auto root = makeRoot();
    addNode (root, "lfo",  "lfo",  "wobble",  1);   // Mono-Out
    addNode (root, "loop", "looper_patch_out", "loop1", 4); // 4 Ausgänge
    addNode (root, "send", conduit::LinkAudioSendModule::staticModuleId, "send1", 0);

    addConnection (root, "lfo", 0, "send", 0);
    REQUIRE (conduit::resolveSourceLabel (root, "send", 0, nullptr) == "wobble");

    // Multi-Output-Quelle → Kanal-Suffix ":{n}" (1-basiert)
    addConnection (root, "loop", 2, "send", 1);
    REQUIRE (conduit::resolveSourceLabel (root, "send", 1, nullptr) == "loop1:3");
}
