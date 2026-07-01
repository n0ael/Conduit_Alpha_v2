#include "LinkAudioSendModule.h"

namespace conduit
{

LinkAudioSendModule::LinkAudioSendModule()
    : NetworkIOModule (BusesProperties()
          .withInput ("Input", juce::AudioChannelSet::discreteChannels (1), true))
{
    // Kein Output-Bus: reiner Sender (Sink-Endpunkt). Das Kanal-Layout des
    // Input-Busses setzt der GraphManager per applySendConfig VOR
    // prepareForGraph (fixe Eingangszahl, kein dynamischer Umbau).
}

LinkAudioSendModule::~LinkAudioSendModule()
{
    cancelPendingUpdate();

    // Der Destruktor läuft erst, wenn der Graph den Node und jede
    // Render-Sequenz-Referenz freigegeben hat — processBlock kann nicht mehr
    // laufen, die Sinks dürfen direkt destruieren.
    for (auto& slot : slots)
        slot->rtSink.store (nullptr);

    retiredSinks.clear();
    slots.clear();
    disableAudioOnce();  // Preset-Load/Shutdown ohne Phase 1 (5.3)
}

//==============================================================================
juce::String LinkAudioSendModule::getModuleId() const          { return staticModuleId; }
juce::String LinkAudioSendModule::getModuleDisplayName() const { return "Link Audio Send"; }
int LinkAudioSendModule::getStateVersion() const               { return stateVersion; }

//==============================================================================
juce::ValueTree LinkAudioSendModule::createState()
{
    auto nodeTree = ConduitModule::createState();
    applyInputConfig (nodeTree, { InputMode::stereo });  // Default: 1 Stereo-Eingang
    return nodeTree;
}

void LinkAudioSendModule::applyInputConfig (juce::ValueTree nodeTree,
                                            const std::vector<InputMode>& modesIn)
{
    // Mindestens ein Eingang — leere Config wäre ein Modul ohne Kanäle.
    std::vector<InputMode> modes = modesIn;
    if (modes.empty())
        modes.push_back (InputMode::stereo);

    // <Inputs> und in*_gain-Parameter frisch aufbauen.
    nodeTree.removeChild (nodeTree.getChildWithName (id::inputs), nullptr);
    juce::ValueTree inputsTree (id::inputs);

    auto params = nodeTree.getChildWithName (id::parameters);
    if (! params.isValid())
    {
        params = juce::ValueTree (id::parameters);
        nodeTree.appendChild (params, nullptr);
    }

    for (int i = params.getNumChildren(); --i >= 0;)
    {
        const auto pid = params.getChild (i).getProperty (id::paramId).toString();
        if (pid.startsWith ("in") && pid.endsWith ("_gain"))
            params.removeChild (i, nullptr);
    }

    int total = 0;
    int n = 0;

    for (const auto mode : modes)
    {
        ++n;
        const int width    = (mode == InputMode::stereo) ? 2 : 1;
        const auto gainId  = "in" + juce::String (n) + "_gain";

        juce::ValueTree in (id::input);
        in.setProperty (id::inputId,          juce::Uuid().toString(),                nullptr);
        in.setProperty (id::inputMode,        width == 2 ? modeStereo : modeMono,     nullptr);
        in.setProperty (id::inputUserName,    juce::String(),                         nullptr);
        in.setProperty (id::inputAutoName,    juce::String(),                         nullptr);
        in.setProperty (id::inputGainParamId, gainId,                                 nullptr);
        inputsTree.appendChild (in, nullptr);

        params.appendChild (makeParameter (gainId, 1.0, 0.0, 1.0, 1.0), nullptr);
        total += width;
    }

    nodeTree.appendChild (inputsTree, nullptr);
    nodeTree.setProperty (id::numInputChannels,  total, nullptr);
    nodeTree.setProperty (id::numOutputChannels, 0,     nullptr);  // reiner Sender
}

std::vector<SendInputConfig> LinkAudioSendModule::readInputConfig (const juce::ValueTree& nodeTree)
{
    std::vector<SendInputConfig> result;
    const auto inputsTree = nodeTree.getChildWithName (id::inputs);

    for (int i = 0; i < inputsTree.getNumChildren(); ++i)
    {
        const auto in = inputsTree.getChild (i);

        SendInputConfig cfg;
        cfg.inputId     = in.getProperty (id::inputId).toString();
        cfg.width       = in.getProperty (id::inputMode).toString() == modeStereo ? 2 : 1;
        cfg.gainParamId = in.getProperty (id::inputGainParamId).toString();

        const auto userName = in.getProperty (id::inputUserName).toString();
        const auto autoName = in.getProperty (id::inputAutoName).toString();
        cfg.effectiveName   = userName.isNotEmpty() ? userName
                            : (autoName.isNotEmpty() ? autoName
                                                     : "input" + juce::String (i + 1));

        result.push_back (std::move (cfg));
    }

    return result;
}

void LinkAudioSendModule::migrate (juce::ValueTree nodeTree)
{
    // Idempotent: neues Schema hat bereits <Inputs>.
    if (nodeTree.getChildWithName (id::inputs).isValid())
        return;

    // Alt: fester Stereo-Send (2 In / 2 Out Pass-Through). Neu: 1 Stereo-
    // Eingang, autoName = alte moduleId (Namensstabilität für Ableton),
    // numOutputChannels = 0 (reiner Sender). Alte Output-Kabel werden vom
    // syncConnections still verworfen (Alpha-Caveat, Plan).
    applyInputConfig (nodeTree, { InputMode::stereo });

    if (auto in = nodeTree.getChildWithName (id::inputs).getChild (0); in.isValid())
        in.setProperty (id::inputAutoName, nodeTree.getProperty (id::moduleId).toString(), nullptr);

    nodeTree.setProperty (id::stateVersion, stateVersion, nullptr);
}

//==============================================================================
void LinkAudioSendModule::applySendConfig (const std::vector<SendInputConfig>& inputs)
{
    JUCE_ASSERT_MESSAGE_THREAD

    pendingConfig = inputs;

    int total = 0;
    for (const auto& cfg : inputs)
        total += juce::jlimit (1, 2, cfg.width);

    total = juce::jmax (1, total);
    setChannelLayoutOfBus (true, 0, juce::AudioChannelSet::discreteChannels (total));
}

void LinkAudioSendModule::setLinkAudioContext (LinkClock* clock, const juce::String& initialModuleId)
{
    JUCE_ASSERT_MESSAGE_THREAD

    linkClock = clock;
    currentModuleId = initialModuleId;
}

void LinkAudioSendModule::moduleIdRenamed (const juce::String& newModuleId)
{
    JUCE_ASSERT_MESSAGE_THREAD

    currentModuleId = newModuleId;

    // Präfix-Wechsel: alle Kanal-Namen folgen live zu den Peers (7.2).
    for (auto& slot : slots)
        if (slot->sink != nullptr)
            slot->sink->setName (sinkNameFor (slot->effectiveName));
}

void LinkAudioSendModule::setClockBus (const ClockBus* bus) noexcept
{
    clockBus = bus;
}

juce::String LinkAudioSendModule::sinkNameFor (const juce::String& effectiveName) const
{
    return currentModuleId.isNotEmpty() ? currentModuleId + "/" + effectiveName
                                        : effectiveName;
}

//==============================================================================
bool LinkAudioSendModule::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Beliebige diskrete Eingangs-Kanalzahl, keine Ausgänge (reiner Sender).
    return layouts.getMainInputChannels() >= 1 && layouts.getMainOutputChannels() == 0;
}

void LinkAudioSendModule::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int block = juce::jmax (1, samplesPerBlock);

    // Slots einmalig aus der injizierten Config aufbauen (fixe Eingangszahl).
    if (slots.empty() && ! pendingConfig.empty())
    {
        auto* clock = linkClock.get();
        int offset = 0;
        int index  = 0;
        bool anySink = false;

        for (const auto& cfg : pendingConfig)
        {
            ++index;
            auto slot = std::make_unique<InputSlot>();
            slot->inputId       = cfg.inputId;
            slot->effectiveName = cfg.effectiveName;
            slot->width         = juce::jlimit (1, 2, cfg.width);
            slot->offset        = offset;
            slot->gainParamId   = cfg.gainParamId;
            // eigener, verschiedener Dither-Seed pro Slot (deterministisch)
            slot->ditherState   = 0x6c078965u + static_cast<std::uint32_t> (index) * 2654435761u;
            offset += slot->width;

            if (clock != nullptr && slot->effectiveName.isNotEmpty())
            {
                slot->sink = clock->createSink (sinkNameFor (slot->effectiveName),
                                                static_cast<std::size_t> (block)
                                                    * static_cast<std::size_t> (slot->width));
                slot->rtSink.store (slot->sink.get());
                slot->status.store (static_cast<int> (SendStatus::announced), std::memory_order_relaxed);
                anySink = true;
            }

            slots.push_back (std::move (slot));
        }

        if (anySink && ! audioEnabled)
        {
            if (clock != nullptr)
                clock->enableAudio (true);  // Refcount: erstes aktives Modul
            audioEnabled = true;
        }
    }
    else
    {
        // Re-Prepare (Graph re-prepariert): Kapazität nur anheben (Link-No-op sonst)
        for (auto& slot : slots)
            if (slot->sink != nullptr)
                slot->sink->requestMaxNumSamples (static_cast<std::size_t> (block)
                                                      * static_cast<std::size_t> (slot->width));
    }

    // Attenuator-Ramp + Scratch/Interleave-Buffer (max stereo) vorallokieren
    for (auto& slot : slots)
    {
        slot->smoothedGain.reset (sampleRate, 0.005);
        slot->smoothedGain.setCurrentAndTargetValue (slot->gainTarget.load (std::memory_order_relaxed));
    }

    scratchLeft.assign  (static_cast<std::size_t> (block), 0.0f);
    scratchRight.assign (static_cast<std::size_t> (block), 0.0f);
    interleavedBuffer.assign (static_cast<std::size_t> (block) * 2, 0);

    updateAggregateStatus();
}

void LinkAudioSendModule::releaseResources()
{
    // Sinks bleiben announced — das Modul ist weiterhin Teil des Patches.
}

//==============================================================================
void LinkAudioSendModule::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Epoch-Handshake: Inkrement VOR dem rtSink-Load (seq_cst)
    blocksProcessed.fetch_add (1);

    const int numFrames          = buffer.getNumSamples();
    const int totalInputChannels = buffer.getNumChannels();

    const bool haveClock = (clockBus != nullptr && numFrames > 0
                            && static_cast<std::size_t> (numFrames)
                                   <= interleavedBuffer.size() / 2);

    for (auto& slotPtr : slots)
    {
        auto& slot = *slotPtr;
        auto* activeSink = slot.rtSink.load();

        if (activeSink == nullptr)
        {
            slot.status.store (static_cast<int> (SendStatus::offline), std::memory_order_relaxed);
            continue;
        }

        const int width = slot.width;

        if (! haveClock || slot.offset + width > totalInputChannels)
        {
            // Ohne Takt-Bus (Tests) / fehlende Kanäle: announced bleiben,
            // kein Commit mit falscher Zeitbasis.
            slot.status.store (static_cast<int> (SendStatus::announced), std::memory_order_relaxed);
            continue;
        }

        // Attenuator: EIN Gain-Ramp pro Frame, auf alle Slot-Kanäle gleich
        // angewandt (SmoothedValue nur einmal pro Frame fortschreiben).
        slot.smoothedGain.setTargetValue (slot.gainTarget.load (std::memory_order_relaxed));
        const float* srcL = buffer.getReadPointer (slot.offset);
        const float* srcR = width == 2 ? buffer.getReadPointer (slot.offset + 1) : nullptr;

        for (int frame = 0; frame < numFrames; ++frame)
        {
            const float g = slot.smoothedGain.getNextValue();
            scratchLeft[static_cast<std::size_t> (frame)] = srcL[frame] * g;
            if (width == 2)
                scratchRight[static_cast<std::size_t> (frame)] = srcR[frame] * g;
        }

        const float* chans[2] = { scratchLeft.data(), scratchRight.data() };
        convertToInt16Tpdf (chans, width, numFrames, interleavedBuffer.data(), slot.ditherState);

        const auto result = activeSink->commitFromClockState (interleavedBuffer.data(),
                                                              numFrames, width,
                                                              clockBus->current);

        slot.status.store (static_cast<int> (result == LinkClock::Sink::CommitResult::committed
                                                 ? SendStatus::streaming
                                                 : SendStatus::announced),
                           std::memory_order_relaxed);
    }

    updateAggregateStatus();
}

void LinkAudioSendModule::convertToInt16Tpdf (const float* const* channelData,
                                              int numChannels, int numFrames,
                                              std::int16_t* dest,
                                              std::uint32_t& ditherState) noexcept
{
    for (int frame = 0; frame < numFrames; ++frame)
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            // TPDF = Differenz zweier Uniform-Werte aus dem LCG (3.1):
            // Dreieck über ±1 LSB, Mittelwert 0 — kein rand(), kein Heap.
            ditherState = ditherState * 1664525u + 1013904223u;
            const auto r1 = static_cast<float> (ditherState >> 8) * (1.0f / 16777216.0f);
            ditherState = ditherState * 1664525u + 1013904223u;
            const auto r2 = static_cast<float> (ditherState >> 8) * (1.0f / 16777216.0f);

            const auto dithered = channelData[channel][frame] * 32767.0f + (r1 - r2);

            *dest++ = static_cast<std::int16_t> (
                juce::roundToInt (juce::jlimit (-32768.0f, 32767.0f, dithered)));
        }
    }
}

//==============================================================================
void LinkAudioSendModule::releaseSessionResources()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Phase 1 (5.3): alle Audio-Threads sofort von den Sinks trennen,
    // Refcount freigeben — Destruktion folgt racefrei über den Handshake.
    for (auto& slot : slots)
    {
        slot->rtSink.store (nullptr);
        slot->status.store (static_cast<int> (SendStatus::offline), std::memory_order_relaxed);
    }

    aggregateStatus.store (static_cast<int> (SendStatus::offline), std::memory_order_relaxed);
    disableAudioOnce();

    bool anyRetired = false;
    for (auto& slot : slots)
        if (slot->sink != nullptr)
        {
            retiredSinks.push_back (std::move (slot->sink));
            anyRetired = true;
        }

    if (! anyRetired)
        return;

    retireEpoch = blocksProcessed.load();
    retireDeadlineMs = juce::Time::getMillisecondCounter() + retireGraceMs;
    triggerAsyncUpdate();
}

void LinkAudioSendModule::handleAsyncUpdate()
{
    if (retiredSinks.empty())
        return;

    // Self-Re-Dispatch (Muster 5.2 Schritt 3): warten, bis nach dem rtSink-
    // Store ein neuer Audio-Block begonnen hat; sonst destruiert die Deadline.
    if (blocksProcessed.load() == retireEpoch
        && juce::Time::getMillisecondCounter() < retireDeadlineMs)
    {
        triggerAsyncUpdate();
        return;
    }

    retiredSinks.clear();  // Kanäle verschwinden jetzt bei den Peers
}

void LinkAudioSendModule::disableAudioOnce()
{
    if (! audioEnabled)
        return;

    audioEnabled = false;

    if (auto* clock = linkClock.get())
        clock->enableAudio (false);  // Refcount: letztes Modul deaktiviert
}

void LinkAudioSendModule::updateAggregateStatus() noexcept
{
    int agg = static_cast<int> (SendStatus::offline);
    for (auto& slot : slots)
        agg = juce::jmax (agg, slot->status.load (std::memory_order_relaxed));

    aggregateStatus.store (agg, std::memory_order_relaxed);
}

//==============================================================================
std::atomic<float>* LinkAudioSendModule::getParameterTarget (const juce::String& parameterId) noexcept
{
    for (auto& slot : slots)
        if (slot->gainParamId == parameterId)
            return &slot->gainTarget;

    return nullptr;
}

//==============================================================================
LinkAudioSendModule::SendStatus LinkAudioSendModule::getSendStatusForUi() const noexcept
{
    return static_cast<SendStatus> (aggregateStatus.load (std::memory_order_relaxed));
}

LinkAudioSendModule::SendStatus LinkAudioSendModule::getSlotStatusForUi (int slotIndex) const noexcept
{
    if (slotIndex < 0 || slotIndex >= static_cast<int> (slots.size()))
        return SendStatus::offline;

    return static_cast<SendStatus> (slots[static_cast<std::size_t> (slotIndex)]
                                        ->status.load (std::memory_order_relaxed));
}

int LinkAudioSendModule::getNumSlots() const noexcept
{
    return static_cast<int> (slots.size());
}

juce::StringArray LinkAudioSendModule::getSinkNames() const
{
    JUCE_ASSERT_MESSAGE_THREAD

    juce::StringArray names;
    for (auto& slot : slots)
        if (slot->sink != nullptr)
            names.add (slot->sink->getName());

    return names;
}

bool LinkAudioSendModule::isSinkRetirePending() const noexcept
{
    return ! retiredSinks.empty();
}

void LinkAudioSendModule::flushPendingSinkRetirement()
{
    JUCE_ASSERT_MESSAGE_THREAD
    handleUpdateNowIfNeeded();
}

} // namespace conduit
