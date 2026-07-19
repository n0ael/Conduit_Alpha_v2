#include "LooperOutModule.h"

#include "Core/Looper/LooperBank.h"

namespace conduit
{

LooperOutModule::LooperOutModule()
    : IOModule (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::discreteChannels (2), true))
{
    // Keine Eingänge: reine Quelle (Looper-Busse der Engine). Das konkrete
    // Kanal-Layout setzt der GraphManager via applyOutputSpecs VOR
    // prepareForGraph.
}

//==============================================================================
juce::String LooperOutModule::getModuleId() const          { return staticModuleId; }
juce::String LooperOutModule::getModuleDisplayName() const { return "Looper Out Mini"; }
int LooperOutModule::getStateVersion() const               { return 1; }

juce::ValueTree LooperOutModule::createState()
{
    auto nodeTree = ConduitModule::createState();

    // Default-Bestückung (User-Spezifikation 18.07.2026): Master stereo +
    // die 4 Looper-Stereo-Paare, alle Post-Fader — Slots sind löschbar
    applyOutputConfig (nodeTree,
                       { { 0, Mode::stereo, false },
                         { 1, Mode::stereo, false },
                         { 2, Mode::stereo, false },
                         { 3, Mode::stereo, false },
                         { 4, Mode::stereo, false } });
    return nodeTree;
}

//==============================================================================
juce::String LooperOutModule::toString (Mode mode)
{
    switch (mode)
    {
        case Mode::sum:    return modeSum;
        case Mode::left:   return modeLeft;
        case Mode::right:  return modeRight;
        case Mode::stereo: break;
    }
    return modeStereo;
}

LooperOutModule::Mode LooperOutModule::modeFromString (const juce::String& text) noexcept
{
    if (text == modeSum)   return Mode::sum;
    if (text == modeLeft)  return Mode::left;
    if (text == modeRight) return Mode::right;
    return Mode::stereo;
}

juce::String LooperOutModule::outputLabel (const OutputSpec& spec)
{
    const auto base = spec.target <= 0 ? juce::String ("Master")
                                       : "Looper " + juce::String (spec.target);
    switch (spec.mode)
    {
        case Mode::sum:    return base + juce::String::fromUTF8 (" · Summe");
        case Mode::left:   return base + juce::String::fromUTF8 (" · L");
        case Mode::right:  return base + juce::String::fromUTF8 (" · R");
        case Mode::stereo: break;
    }
    return base;
}

void LooperOutModule::applyOutputConfig (juce::ValueTree nodeTree,
                                         const std::vector<OutputSpec>& specsIn,
                                         juce::UndoManager* undo)
{
    std::vector<OutputSpec> specs = specsIn;
    if (specs.empty())
        specs.push_back ({ 0, Mode::stereo, false });

    nodeTree.removeChild (nodeTree.getChildWithName (id::outputs), undo);
    juce::ValueTree outputsTree (id::outputs);

    int total = 0;
    for (const auto& spec : specs)
    {
        juce::ValueTree out (id::output);
        out.setProperty (id::outputId,     juce::Uuid().toString(),                 nullptr);
        out.setProperty (id::outputTarget, juce::jlimit (0, LooperBank::maxLoopers,
                                                         spec.target),             nullptr);
        out.setProperty (id::outputMode,   toString (spec.mode),                    nullptr);
        out.setProperty (id::outputPre,    spec.pre,                                nullptr);
        outputsTree.appendChild (out, nullptr);

        total += widthOf (spec.mode);
    }

    nodeTree.appendChild (outputsTree, undo);
    nodeTree.setProperty (id::numInputChannels,  0,     undo);   // reine Quelle
    nodeTree.setProperty (id::numOutputChannels, total, undo);
}

std::vector<LooperOutModule::OutputSpec> LooperOutModule::readOutputConfig (const juce::ValueTree& nodeTree)
{
    std::vector<OutputSpec> result;
    const auto outputsTree = nodeTree.getChildWithName (id::outputs);

    for (int i = 0; i < outputsTree.getNumChildren(); ++i)
    {
        const auto out = outputsTree.getChild (i);

        OutputSpec spec;
        spec.target = juce::jlimit (0, LooperBank::maxLoopers,
                                    (int) out.getProperty (id::outputTarget, 0));
        spec.mode   = modeFromString (out.getProperty (id::outputMode).toString());
        spec.pre    = (bool) out.getProperty (id::outputPre, false);
        result.push_back (spec);
    }

    return result;
}

void LooperOutModule::appendOutput (juce::ValueTree nodeTree, OutputSpec spec,
                                    juce::UndoManager* undo)
{
    auto outputsTree = nodeTree.getChildWithName (id::outputs);
    if (! outputsTree.isValid())
    {
        applyOutputConfig (nodeTree, { spec }, undo);
        return;
    }

    juce::ValueTree out (id::output);
    out.setProperty (id::outputId,     juce::Uuid().toString(),                 nullptr);
    out.setProperty (id::outputTarget, juce::jlimit (0, LooperBank::maxLoopers,
                                                     spec.target),             nullptr);
    out.setProperty (id::outputMode,   toString (spec.mode),                    nullptr);
    out.setProperty (id::outputPre,    spec.pre,                                nullptr);
    outputsTree.appendChild (out, undo);

    nodeTree.setProperty (id::numOutputChannels,
                          (int) nodeTree.getProperty (id::numOutputChannels, 0)
                              + widthOf (spec.mode),
                          undo);
}

void LooperOutModule::removeOutput (juce::ValueTree nodeTree, int index,
                                    juce::UndoManager* undo)
{
    auto outputsTree = nodeTree.getChildWithName (id::outputs);
    if (! outputsTree.isValid() || outputsTree.getNumChildren() <= 1
        || ! juce::isPositiveAndBelow (index, outputsTree.getNumChildren()))
        return;   // letzter Slot bleibt

    const auto out = outputsTree.getChild (index);
    const auto width = widthOf (modeFromString (out.getProperty (id::outputMode).toString()));
    outputsTree.removeChild (index, undo);

    nodeTree.setProperty (id::numOutputChannels,
                          juce::jmax (1, (int) nodeTree.getProperty (id::numOutputChannels, 0)
                                             - width),
                          undo);
}

//==============================================================================
void LooperOutModule::setLooperAudioSource (LooperBank* bank)
{
    JUCE_ASSERT_MESSAGE_THREAD
    rtBank.store (bank, std::memory_order_release);
}

void LooperOutModule::applyOutputSpecs (const std::vector<OutputSpec>& specsIn)
{
    JUCE_ASSERT_MESSAGE_THREAD

    specs = specsIn;
    if (specs.empty())
        specs.push_back ({ 0, Mode::stereo, false });

    int total = 0;
    for (const auto& spec : specs)
        total += widthOf (spec.mode);

    totalChannels = juce::jmax (1, total);
    setChannelLayoutOfBus (false, 0, juce::AudioChannelSet::discreteChannels (totalChannels));
}

bool LooperOutModule::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannels() == 0
        && layouts.getMainOutputChannels() >= 1;
}

void LooperOutModule::prepareToPlay (double, int)
{
    // Keine eigenen Puffer — die Busse leben bei der LooperBank
}

void LooperOutModule::releaseResources()
{
}

//==============================================================================
void LooperOutModule::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
        const auto width = widthOf (spec.mode);
        if (channel + width > buffer.getNumChannels())
            break;

        // Quell-Busse: Master ODER Pre-/Post-Bus des Ziel-Loopers
        const float* sourceLeft  = nullptr;
        const float* sourceRight = nullptr;

        if (spec.target <= 0)
        {
            sourceLeft  = view.master[0];
            sourceRight = view.master[1];
        }
        else
        {
            const auto l = static_cast<std::size_t> (
                juce::jlimit (0, LooperBank::maxLoopers - 1, spec.target - 1));
            sourceLeft  = spec.pre ? view.pre[l][0] : view.post[l][0];
            sourceRight = spec.pre ? view.pre[l][1] : view.post[l][1];
        }

        if (sourceLeft == nullptr || sourceRight == nullptr)
        {
            channel += width;
            continue;
        }

        switch (spec.mode)
        {
            case Mode::stereo:
                juce::FloatVectorOperations::copy (buffer.getWritePointer (channel),
                                                   sourceLeft, numSamples);
                juce::FloatVectorOperations::copy (buffer.getWritePointer (channel + 1),
                                                   sourceRight, numSamples);
                break;

            case Mode::sum:
            {
                auto* dest = buffer.getWritePointer (channel);
                for (int i = 0; i < numSamples; ++i)
                    dest[i] = 0.5f * (sourceLeft[i] + sourceRight[i]);
                break;
            }

            case Mode::left:
                juce::FloatVectorOperations::copy (buffer.getWritePointer (channel),
                                                   sourceLeft, numSamples);
                break;

            case Mode::right:
                juce::FloatVectorOperations::copy (buffer.getWritePointer (channel),
                                                   sourceRight, numSamples);
                break;
        }

        channel += width;
    }
}

} // namespace conduit
