#include "LooperPatchInModule.h"

namespace conduit
{

LooperPatchInModule::LooperPatchInModule()
    : IOModule (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::discreteChannels (2), true)
          .withOutput ("Output", juce::AudioChannelSet::discreteChannels (2), true))
{
    for (auto& slot : rtSlots)
        slot.store (-1, std::memory_order_relaxed);
}

LooperPatchInModule::~LooperPatchInModule()
{
    // Preset-Load/Shutdown ohne Phase 1: Kanäle trotzdem deregistrieren —
    // laufendes Material bleibt beim Service als "held" (CaptureTapModule)
    unregisterChannels();
}

//==============================================================================
juce::String LooperPatchInModule::getModuleId() const          { return staticModuleId; }
juce::String LooperPatchInModule::getModuleDisplayName() const { return "Looper patch IN"; }
int LooperPatchInModule::getStateVersion() const               { return 1; }

juce::ValueTree LooperPatchInModule::createState()
{
    // Default-Bestückung (User-Entscheidung 19.07.2026): 4× stereo +
    // 4× mono = 12 Kanäle — genug für ein typisches Set ohne Slot-Umbau;
    // die CaptureService-Slot-Reserve deckt genau diese Bestückung ab
    auto nodeTree = ConduitModule::createState();
    applyInputConfig (nodeTree, { InputMode::stereo, InputMode::stereo,
                                  InputMode::stereo, InputMode::stereo,
                                  InputMode::mono, InputMode::mono,
                                  InputMode::mono, InputMode::mono });
    return nodeTree;
}

//==============================================================================
void LooperPatchInModule::applyInputConfig (juce::ValueTree nodeTree,
                                       const std::vector<InputMode>& modesIn,
                                       juce::UndoManager* undo)
{
    std::vector<InputMode> modes = modesIn;
    if (modes.empty())
        modes.push_back (InputMode::stereo);

    nodeTree.removeChild (nodeTree.getChildWithName (id::inputs), undo);
    juce::ValueTree inputsTree (id::inputs);

    int total = 0;
    int n = 0;

    for (const auto mode : modes)
    {
        ++n;
        const int width = mode == InputMode::stereo ? 2 : 1;

        juce::ValueTree in (id::input);
        in.setProperty (id::inputId,       juce::Uuid().toString(),            nullptr);
        in.setProperty (id::inputMode,     width == 2 ? modeStereo : modeMono, nullptr);
        in.setProperty (id::inputUserName, juce::String(),                     nullptr);
        in.setProperty (id::inputAutoName, "In " + juce::String (n),           nullptr);
        inputsTree.appendChild (in, nullptr);

        total += width;
    }

    nodeTree.appendChild (inputsTree, undo);
    nodeTree.setProperty (id::numInputChannels,  total, undo);
    nodeTree.setProperty (id::numOutputChannels, total, undo);
}

void LooperPatchInModule::appendInput (juce::ValueTree nodeTree, InputMode mode,
                                  juce::UndoManager* undo)
{
    auto inputsTree = nodeTree.getChildWithName (id::inputs);
    if (! inputsTree.isValid())
    {
        applyInputConfig (nodeTree, { mode }, undo);
        return;
    }

    const int width = mode == InputMode::stereo ? 2 : 1;
    const auto n = inputsTree.getNumChildren() + 1;

    juce::ValueTree in (id::input);
    in.setProperty (id::inputId,       juce::Uuid().toString(),            nullptr);
    in.setProperty (id::inputMode,     width == 2 ? modeStereo : modeMono, nullptr);
    in.setProperty (id::inputUserName, juce::String(),                     nullptr);
    in.setProperty (id::inputAutoName, "In " + juce::String (n),           nullptr);
    inputsTree.appendChild (in, undo);

    const auto total = (int) nodeTree.getProperty (id::numInputChannels, 0) + width;
    nodeTree.setProperty (id::numInputChannels,  total, undo);
    nodeTree.setProperty (id::numOutputChannels, total, undo);
}

void LooperPatchInModule::removeInput (juce::ValueTree nodeTree, int index,
                                  juce::UndoManager* undo)
{
    auto inputsTree = nodeTree.getChildWithName (id::inputs);
    if (! inputsTree.isValid() || inputsTree.getNumChildren() <= 1
        || ! juce::isPositiveAndBelow (index, inputsTree.getNumChildren()))
        return;   // letzter Slot bleibt — Modul ohne Kanäle wäre ungültig

    const auto in = inputsTree.getChild (index);
    const int width = in.getProperty (id::inputMode).toString() == modeStereo ? 2 : 1;
    inputsTree.removeChild (index, undo);

    const auto total = juce::jmax (1, (int) nodeTree.getProperty (id::numInputChannels, 0) - width);
    nodeTree.setProperty (id::numInputChannels,  total, undo);
    nodeTree.setProperty (id::numOutputChannels, total, undo);
}

juce::String LooperPatchInModule::effectiveInputName (const juce::ValueTree& inputTree, int index)
{
    const auto userName = inputTree.getProperty (id::inputUserName).toString();
    if (userName.isNotEmpty())
        return userName;

    const auto autoName = inputTree.getProperty (id::inputAutoName).toString();
    if (autoName.isNotEmpty())
        return autoName;

    return "In " + juce::String (index + 1);
}

juce::String LooperPatchInModule::tapBaseName (const juce::String& moduleId,
                                          const juce::String& effectiveName)
{
    return moduleId + "/" + effectiveName;
}

//==============================================================================
void LooperPatchInModule::applySendConfig (const std::vector<SendInputConfig>& inputs)
{
    JUCE_ASSERT_MESSAGE_THREAD

    slots.clear();
    int offset = 0;

    for (const auto& cfg : inputs)
    {
        Slot slot;
        slot.inputId       = cfg.inputId;
        slot.effectiveName = cfg.effectiveName;
        slot.offset        = offset;
        slot.width         = juce::jlimit (1, 2, cfg.width);
        offset += slot.width;
        slots.push_back (std::move (slot));
    }

    totalChannels = juce::jmax (1, offset);

    // Beide Busse ATOMAR wechseln — einzelne setChannelLayoutOfBus-Aufrufe
    // scheitern am Pass-Through-Kontrakt (isBusesLayoutSupported: in == out)
    auto layout = getBusesLayout();
    layout.inputBuses.set  (0, juce::AudioChannelSet::discreteChannels (totalChannels));
    layout.outputBuses.set (0, juce::AudioChannelSet::discreteChannels (totalChannels));
    setBusesLayout (layout);
}

void LooperPatchInModule::inputNameChanged (const juce::String& inputId,
                                       const juce::String& effectiveName)
{
    JUCE_ASSERT_MESSAGE_THREAD

    for (auto& slot : slots)
        if (slot.inputId == inputId)
        {
            slot.effectiveName = effectiveName;
            refreshChannelNames();   // Spurnamen folgen live (Muster 7.2)
            return;
        }
}

//==============================================================================
void LooperPatchInModule::setCaptureTapContext (CaptureService* serviceToUse,
                                           const juce::String& initialModuleId)
{
    service = serviceToUse;
    currentModuleId = initialModuleId;
}

void LooperPatchInModule::captureModuleIdRenamed (const juce::String& newModuleId)
{
    currentModuleId = newModuleId;
    refreshChannelNames();
}

void LooperPatchInModule::releaseCaptureResources()
{
    unregisterChannels();
}

juce::String LooperPatchInModule::channelNameFor (const Slot& slot, int channelInSlot) const
{
    const auto base = tapBaseName (currentModuleId, slot.effectiveName);
    if (slot.width == 1)
        return base;

    return base + (channelInSlot == 0 ? "_l" : "_r");
}

void LooperPatchInModule::refreshChannelNames()
{
    if (service == nullptr)
        return;

    std::size_t handleIndex = 0;
    for (const auto& slot : slots)
        for (int ch = 0; ch < slot.width && handleIndex < handles.size(); ++ch, ++handleIndex)
            if (handles[handleIndex].isValid())
                service->setVirtualChannelName (handles[handleIndex],
                                                channelNameFor (slot, ch));
}

void LooperPatchInModule::unregisterChannels()
{
    // Audio-Thread zuerst trennen — in-flight Blöcke prallen am
    // writerActive-Atomic des Slots ab (Muster CaptureTapModule)
    rtService.store (nullptr, std::memory_order_release);
    for (auto& slot : rtSlots)
        slot.store (-1, std::memory_order_release);

    if (service != nullptr)
        for (auto& handle : handles)
            if (handle.isValid())
                service->unregisterVirtualChannel (handle);

    handles.clear();
}

//==============================================================================
juce::Result LooperPatchInModule::prepareForGraph (double sampleRate, int maximumBlockSize)
{
    if (const auto result = IOModule::prepareForGraph (sampleRate, maximumBlockSize);
        result.failed())
        return result;

    // Idempotent (Kontrakt 5.2 Schritt 1): bereits registrierte Kanäle behalten
    if (service == nullptr || isTapRegistered())
        return juce::Result::ok();

    handles.clear();
    for (const auto& slot : slots)
        for (int ch = 0; ch < slot.width; ++ch)
        {
            auto handle = service->registerVirtualChannel (channelNameFor (slot, ch));

            if (! handle.isValid())
            {
                unregisterChannels();
                return juce::Result::fail (juce::String::fromUTF8 (
                    "Keine freien Capture-Kan\xc3\xa4le f\xc3\xbcr Looper In (max. ")
                    + juce::String (CaptureService::MAX_VIRTUAL_CHANNELS) + ")");
            }

            handles.push_back (handle);
        }

    for (std::size_t ch = 0; ch < handles.size() && ch < rtSlots.size(); ++ch)
        rtSlots[ch].store (handles[ch].slot, std::memory_order_release);

    rtService.store (service, std::memory_order_release);
    return juce::Result::ok();
}

bool LooperPatchInModule::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Pass-Through: Ein- und Ausgang tragen dieselbe (diskrete) Kanalzahl
    return layouts.getMainInputChannels() >= 1
        && layouts.getMainInputChannels() == layouts.getMainOutputChannels();
}

void LooperPatchInModule::prepareToPlay (double, int)
{
    // Keine eigenen Puffer — Meter/Gate/Ring leben beim CaptureService
}

void LooperPatchInModule::releaseResources()
{
}

//==============================================================================
void LooperPatchInModule::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Pass-Through: der Buffer bleibt unangetastet — nur lesend abgreifen
    auto* capture = rtService.load (std::memory_order_acquire);
    if (capture == nullptr)
        return;

    const auto numSamples = buffer.getNumSamples();
    const auto channels = juce::jmin (totalChannels, buffer.getNumChannels(),
                                      (int) rtSlots.size());

    for (int ch = 0; ch < channels; ++ch)
        capture->writeVirtualChannel (
            { rtSlots[static_cast<std::size_t> (ch)].load (std::memory_order_acquire) },
            buffer.getReadPointer (ch), numSamples);
}

} // namespace conduit
