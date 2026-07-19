#include "LooperPatchOutModule.h"

#include "Core/Looper/LooperBank.h"

namespace conduit
{

LooperPatchOutModule::LooperPatchOutModule()
    : IOModule (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::discreteChannels (2), true))
{
    // Keine Eingänge: reine Quelle (Looper-Busse der Engine). Das konkrete
    // Kanal-Layout setzt der GraphManager via applyOutputSpecs VOR
    // prepareForGraph.
}

//==============================================================================
juce::String LooperPatchOutModule::getModuleId() const          { return staticModuleId; }
juce::String LooperPatchOutModule::getModuleDisplayName() const { return "Looper patch OUT"; }
int LooperPatchOutModule::getStateVersion() const               { return 1; }

juce::ValueTree LooperPatchOutModule::createState()
{
    auto nodeTree = ConduitModule::createState();

    // Minimal-Default (1 Looper / 1 Track) — addModuleNode synct sofort
    // auf die echte Struktur (syncLooperPatchOutConfigs)
    applyOutputConfig (nodeTree, buildSpecs (Structure {}));
    return nodeTree;
}

//==============================================================================
juce::String LooperPatchOutModule::toString (Kind kind)
{
    switch (kind)
    {
        case Kind::track:  return kindTrack;
        case Kind::bus:    return kindBus;
        case Kind::send:   return kindSend;
        case Kind::master: break;
    }
    return kindMaster;
}

LooperPatchOutModule::Kind LooperPatchOutModule::kindFromString (const juce::String& text) noexcept
{
    if (text == kindTrack) return Kind::track;
    if (text == kindBus)   return Kind::bus;
    if (text == kindSend)  return Kind::send;
    return Kind::master;
}

juce::String LooperPatchOutModule::outputLabel (const OutputSpec& spec)
{
    switch (spec.kind)
    {
        case Kind::track:
            return "Looper " + juce::String (spec.looper)
                 + juce::String::fromUTF8 (" · Track ") + juce::String (spec.track);
        case Kind::bus:
            return "Looper " + juce::String (spec.looper)
                 + juce::String::fromUTF8 (" · Bus");
        case Kind::send:
            return "Send " + juce::String (spec.send);
        case Kind::master:
            break;
    }
    return "Master";
}

std::vector<LooperPatchOutModule::OutputSpec> LooperPatchOutModule::buildSpecs (const Structure& structure)
{
    std::vector<OutputSpec> specs;

    const auto numLoopers = juce::jlimit (1, LooperBank::maxLoopers, structure.numLoopers);

    // Track-Outs, geflattet Looper-major
    for (int l = 1; l <= numLoopers; ++l)
    {
        const auto numTracks = juce::jlimit (1, LooperBank::maxTracks,
                                             structure.numTracks[(size_t) (l - 1)]);
        for (int t = 1; t <= numTracks; ++t)
            specs.push_back ({ Kind::track, l, t, 0 });
    }

    // Bus-Outs pro Looper
    for (int l = 1; l <= numLoopers; ++l)
        specs.push_back ({ Kind::bus, l, 0, 0 });

    // Send-Outs IMMER alle 4 (stabile Kanal-Indizes)
    for (int s = 1; s <= LooperBank::maxSends; ++s)
        specs.push_back ({ Kind::send, 0, 0, s });

    specs.push_back ({ Kind::master, 0, 0, 0 });
    return specs;
}

void LooperPatchOutModule::applyOutputConfig (juce::ValueTree nodeTree,
                                            const std::vector<OutputSpec>& specsIn,
                                            juce::UndoManager* undo)
{
    std::vector<OutputSpec> specs = specsIn;
    if (specs.empty())
        specs.push_back ({ Kind::master, 0, 0, 0 });

    nodeTree.removeChild (nodeTree.getChildWithName (id::outputs), undo);
    juce::ValueTree outputsTree (id::outputs);

    for (const auto& spec : specs)
    {
        juce::ValueTree out (id::output);
        out.setProperty (id::outputId,   juce::Uuid().toString(), nullptr);
        out.setProperty (id::outputKind, toString (spec.kind),    nullptr);

        // outputTarget = Looper-Nr. (track/bus) bzw. Send-Nr. (send)
        switch (spec.kind)
        {
            case Kind::track:
                out.setProperty (id::outputTarget, spec.looper, nullptr);
                out.setProperty (id::outputTrack,  spec.track,  nullptr);
                break;
            case Kind::bus:
                out.setProperty (id::outputTarget, spec.looper, nullptr);
                break;
            case Kind::send:
                out.setProperty (id::outputTarget, spec.send, nullptr);
                break;
            case Kind::master:
                out.setProperty (id::outputTarget, 0, nullptr);
                break;
        }

        outputsTree.appendChild (out, nullptr);
    }

    nodeTree.appendChild (outputsTree, undo);
    nodeTree.setProperty (id::numInputChannels, 0, undo);   // reine Quelle
    nodeTree.setProperty (id::numOutputChannels,
                          (int) specs.size() * slotWidth, undo);
}

std::vector<LooperPatchOutModule::OutputSpec> LooperPatchOutModule::readOutputConfig (const juce::ValueTree& nodeTree)
{
    std::vector<OutputSpec> result;
    const auto outputsTree = nodeTree.getChildWithName (id::outputs);

    for (int i = 0; i < outputsTree.getNumChildren(); ++i)
    {
        const auto out = outputsTree.getChild (i);
        const auto target = (int) out.getProperty (id::outputTarget, 0);

        OutputSpec spec;
        spec.kind = kindFromString (out.getProperty (id::outputKind).toString());
        switch (spec.kind)
        {
            case Kind::track:
                spec.looper = juce::jlimit (1, LooperBank::maxLoopers, target);
                spec.track  = juce::jlimit (1, LooperBank::maxTracks,
                                            (int) out.getProperty (id::outputTrack, 1));
                break;
            case Kind::bus:
                spec.looper = juce::jlimit (1, LooperBank::maxLoopers, target);
                break;
            case Kind::send:
                spec.send = juce::jlimit (1, LooperBank::maxSends, target);
                break;
            case Kind::master:
                break;
        }

        result.push_back (spec);
    }

    return result;
}

int LooperPatchOutModule::channelOffsetOf (const std::vector<OutputSpec>& specs,
                                         const OutputSpec& spec) noexcept
{
    for (std::size_t i = 0; i < specs.size(); ++i)
        if (specs[i] == spec)
            return (int) i * slotWidth;

    return -1;
}

//==============================================================================
void LooperPatchOutModule::setLooperAudioSource (LooperBank* bank)
{
    JUCE_ASSERT_MESSAGE_THREAD
    rtBank.store (bank, std::memory_order_release);
}

void LooperPatchOutModule::applyOutputSpecs (const std::vector<OutputSpec>& specsIn)
{
    JUCE_ASSERT_MESSAGE_THREAD

    specs = specsIn;
    if (specs.empty())
        specs.push_back ({ Kind::master, 0, 0, 0 });

    totalChannels = (int) specs.size() * slotWidth;
    setChannelLayoutOfBus (false, 0, juce::AudioChannelSet::discreteChannels (totalChannels));
}

bool LooperPatchOutModule::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannels() == 0
        && layouts.getMainOutputChannels() >= 2;
}

void LooperPatchOutModule::prepareToPlay (double, int)
{
    // Keine eigenen Puffer — die Busse leben bei der LooperBank
}

void LooperPatchOutModule::releaseResources()
{
}

//==============================================================================
void LooperPatchOutModule::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();   // reine Quelle — undefinierten Graph-Buffer überschreiben

    auto* bank = rtBank.load (std::memory_order_acquire);
    if (bank == nullptr)
        return;

    const auto view = bank->getAudioView();
    const auto numSamples = juce::jmin (view.numSamples, buffer.getNumSamples());
    if (numSamples <= 0)
        return;

    int channel = 0;
    for (const auto& spec : specs)
    {
        if (channel + slotWidth > buffer.getNumChannels())
            break;

        const float* sourceLeft  = nullptr;
        const float* sourceRight = nullptr;

        switch (spec.kind)
        {
            case Kind::track:
            {
                const auto l = (std::size_t) juce::jlimit (0, LooperBank::maxLoopers - 1,
                                                           spec.looper - 1);
                const auto t = (std::size_t) juce::jlimit (0, LooperBank::maxTracks - 1,
                                                           spec.track - 1);
                sourceLeft  = view.track[l][t][0];
                sourceRight = view.track[l][t][1];
                break;
            }

            case Kind::bus:
            {
                const auto l = (std::size_t) juce::jlimit (0, LooperBank::maxLoopers - 1,
                                                           spec.looper - 1);
                sourceLeft  = view.post[l][0];
                sourceRight = view.post[l][1];
                break;
            }

            case Kind::send:
            {
                const auto s = (std::size_t) juce::jlimit (0, LooperBank::maxSends - 1,
                                                           spec.send - 1);
                sourceLeft  = view.send[s][0];
                sourceRight = view.send[s][1];
                break;
            }

            case Kind::master:
                sourceLeft  = view.master[0];
                sourceRight = view.master[1];
                break;
        }

        if (sourceLeft != nullptr && sourceRight != nullptr)
        {
            juce::FloatVectorOperations::copy (buffer.getWritePointer (channel),
                                               sourceLeft, numSamples);
            juce::FloatVectorOperations::copy (buffer.getWritePointer (channel + 1),
                                               sourceRight, numSamples);
        }

        channel += slotWidth;
    }
}

} // namespace conduit
