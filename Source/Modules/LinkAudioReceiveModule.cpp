#include "LinkAudioReceiveModule.h"

namespace conduit
{

LinkAudioReceiveModule::LinkAudioReceiveModule()
    : NetworkIOModule (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::discreteChannels (2), true))
{
    // Reine Quelle: keine Eingänge, festes Stereo-Paar am Ausgang.
}

LinkAudioReceiveModule::~LinkAudioReceiveModule()
{
    // Preset-Load/Shutdown ohne Phase 1 (Muster LinkSendTaps-Destruktor):
    // Ressourcen direkt freigeben. Das finale enableAudio(false) läuft in
    // der LinkClock ohnehin um einen Message-Loop-Hop verzögert — beim
    // App-Shutdown übernimmt ihr Destruktor racefrei (7.2 Teardown-Race).
    releaseSessionResources();
}

//==============================================================================
juce::String LinkAudioReceiveModule::getModuleId() const          { return staticModuleId; }
juce::String LinkAudioReceiveModule::getModuleDisplayName() const { return "Link Audio Receive"; }
int LinkAudioReceiveModule::getStateVersion() const               { return 1; }

juce::ValueTree LinkAudioReceiveModule::createState()
{
    auto nodeTree = ConduitModule::createState();

    // Kanal-Wunsch als Namen (Schema 6.2) — leer bis der User wählt
    nodeTree.setProperty (id::targetPeer,    juce::String(), nullptr);
    nodeTree.setProperty (id::targetChannel, juce::String(), nullptr);

    auto params = nodeTree.getChildWithName (id::parameters);
    params.appendChild (makeParameter (latencyParamId,
                                       latencyDefaultMs, latencyMinMs,
                                       latencyMaxMs, latencyDefaultMs),
                        nullptr);
    return nodeTree;
}

//==============================================================================
void LinkAudioReceiveModule::setLinkAudioContext (LinkClock* clock, const juce::String&)
{
    JUCE_ASSERT_MESSAGE_THREAD

    linkClock = clock;

    if (clock != nullptr)
    {
        // Discovery + Empfang brauchen aktives Link Audio — Refcount halten,
        // solange der Kontext lebt (Idle ist gratis, 7.2).
        clock->enableAudio (true);
        audioEnabledHeld = true;
        clock->addChangeListener (this);
    }

    rebind();
}

void LinkAudioReceiveModule::moduleIdRenamed (const juce::String&)
{
    // Receive announced keine Kanäle — nichts zu propagieren.
}

void LinkAudioReceiveModule::releaseSessionResources()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Phase 1 (5.3): Source-Reset stoppt den Link-Thread-Callback synchron —
    // danach schreibt niemand mehr in den Stream (kein Handshake nötig,
    // anders als beim Sink-Retire; docs/LinkAudio.md).
    source.reset();
    stream.requestReset();
    bindState.store (0, std::memory_order_relaxed);

    // WeakReference: eine bereits zerstörte Clock (Rig-/Shutdown-Teardown)
    // hat ihre Listener-Liste und den Refcount mitgenommen — nichts zu tun.
    if (auto* clock = linkClock.get())
    {
        clock->removeChangeListener (this);

        if (audioEnabledHeld)
            clock->enableAudio (false);
    }

    audioEnabledHeld = false;
    linkClock = nullptr;
}

void LinkAudioReceiveModule::setClockBus (const ClockBus* bus) noexcept
{
    clockBus = bus;
}

//==============================================================================
void LinkAudioReceiveModule::setTargetChannel (const juce::String& peer, const juce::String& channel)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (targetPeer == peer && targetChannel == channel)
        return;

    targetPeer    = peer;
    targetChannel = channel;
    rebind();
}

std::vector<LinkClock::ChannelInfo> LinkAudioReceiveModule::getAvailableChannelsForUi() const
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (auto* clock = linkClock.get())
        return clock->availableChannels();

    return {};
}

void LinkAudioReceiveModule::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // ChannelsChanged (LinkClock marshallt auf den Message Thread, 7.2)
    rebind();
}

LinkClock::ChannelKey LinkAudioReceiveModule::findChannelKey (
    const std::vector<LinkClock::ChannelInfo>& channels,
    const juce::String& peer, const juce::String& channel) noexcept
{
    for (const auto& info : channels)
        if (info.peerName == peer && info.name == channel)
            return info.id;

    return 0;
}

void LinkAudioReceiveModule::rebind()
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto* clock = linkClock.get();

    if (clock == nullptr || (targetPeer.isEmpty() && targetChannel.isEmpty()))
    {
        if (source != nullptr)
        {
            source.reset();
            stream.requestReset();
        }
        bindState.store (0, std::memory_order_relaxed);
        return;
    }

    const auto wanted = findChannelKey (clock->availableChannels(),
                                        targetPeer, targetChannel);

    if (wanted == 0)
    {
        // Wunsch gesetzt, Kanal (noch) nicht da — Bindung lösen, weiter suchen
        if (source != nullptr)
        {
            source.reset();
            stream.requestReset();
        }
        bindState.store (1, std::memory_order_relaxed);
        return;
    }

    if (source == nullptr || source->channelId() != wanted)
    {
        source.reset();
        stream.requestReset();

        // Callback läuft auf einem Link-Thread (7.2): nur Beat-Filter +
        // memcpy in die SpscQueue. Buffer fremder Sessions (nullopt) sind
        // nicht alignbar und werden verworfen (v1-Drift-Lektion).
        source = clock->createSource (wanted,
            [this] (const LinkClock::Source::ReceivedBuffer& rb)
            {
                if (rb.beatAtBufferBegin.has_value())
                    stream.pushBuffer (rb.interleavedSamples, rb.numFrames,
                                       rb.numChannels, rb.sampleRate, rb.tempo,
                                       *rb.beatAtBufferBegin, rb.count);
            });
    }

    bindState.store (2, std::memory_order_relaxed);
}

//==============================================================================
bool LinkAudioReceiveModule::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannels() == 0 && layouts.getMainOutputChannels() == 2;
}

void LinkAudioReceiveModule::prepareToPlay (double, int)
{
    // Der Stream hat feste Kapazitäten (Konstruktor) und rendert unabhängig
    // von der Blockgröße; ein Neustart der Audio-Engine erzeugt höchstens
    // einen Beat-Sprung, den der Render-Reset selbst abfängt.
}

void LinkAudioReceiveModule::releaseResources()
{
    // Source bleibt abonniert — das Modul ist weiterhin Teil des Patches.
}

void LinkAudioReceiveModule::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numFrames = buffer.getNumSamples();

    if (buffer.getNumChannels() < 2 || numFrames <= 0)
    {
        buffer.clear();
        return;
    }

    if (clockBus == nullptr)
    {
        // Ohne Takt-Bus (Tests) gibt es keine Beat-Achse zum Alignen.
        buffer.clear();
        return;
    }

    // DSP clamped IMMER auf min/max (4.6-Grundsatz) — der Atomic kann roh
    // von OSC kommen.
    const float latencyMs = juce::jlimit (latencyMinMs, latencyMaxMs,
                                          latencyTarget.load (std::memory_order_relaxed));

    stream.renderBlock (buffer.getWritePointer (0), buffer.getWritePointer (1),
                        numFrames, clockBus->current, latencyMs);
}

//==============================================================================
std::atomic<float>* LinkAudioReceiveModule::getParameterTarget (const juce::String& parameterId) noexcept
{
    return parameterId == latencyParamId ? &latencyTarget : nullptr;
}

LinkAudioReceiveModule::ReceiveStatus LinkAudioReceiveModule::getReceiveStatusForUi() const noexcept
{
    switch (bindState.load (std::memory_order_relaxed))
    {
        case 0:  return ReceiveStatus::offline;
        case 1:  return ReceiveStatus::searching;
        default: break;
    }

    // Gebunden: der Stream-Status entscheidet zwischen waiting und streaming
    // (idle == abonniert, aber noch kein Traffic — für die UI "waiting").
    return stream.getStatusForUi() == LinkReceiveStream::Status::streaming
             ? ReceiveStatus::streaming
             : ReceiveStatus::waiting;
}

} // namespace conduit
