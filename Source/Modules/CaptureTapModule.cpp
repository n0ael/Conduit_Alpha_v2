#include "CaptureTapModule.h"

namespace conduit
{

CaptureTapModule::CaptureTapModule()
    : UtilityModule (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

CaptureTapModule::~CaptureTapModule()
{
    // Preset-Load/Shutdown ohne Phase 1: Kanäle trotzdem deregistrieren —
    // laufendes Material bleibt beim Service als "held" erhalten
    unregisterChannels();
}

//==============================================================================
juce::String CaptureTapModule::getModuleId() const          { return staticModuleId; }
juce::String CaptureTapModule::getModuleDisplayName() const { return "Capture Tap"; }
int CaptureTapModule::getStateVersion() const               { return 1; }

juce::String CaptureTapModule::channelNameFor (const juce::String& moduleId, int channel)
{
    return moduleId + (channel == 0 ? "_l" : "_r");
}

//==============================================================================
void CaptureTapModule::setCaptureTapContext (CaptureService* serviceToUse,
                                             const juce::String& initialModuleId)
{
    service = serviceToUse;
    currentModuleId = initialModuleId;
}

void CaptureTapModule::captureModuleIdRenamed (const juce::String& newModuleId)
{
    currentModuleId = newModuleId;

    if (service == nullptr)
        return;

    for (size_t ch = 0; ch < handles.size(); ++ch)
        if (handles[ch].isValid())
            service->setVirtualChannelName (handles[ch],
                                            channelNameFor (currentModuleId,
                                                            static_cast<int> (ch)));
}

void CaptureTapModule::releaseCaptureResources()
{
    unregisterChannels();
}

void CaptureTapModule::unregisterChannels()
{
    // Audio-Thread zuerst trennen — ein bereits laufender Block prallt am
    // writerActive-Atomic des Slots ab (writeVirtualChannel, kein Race)
    rtService.store (nullptr, std::memory_order_release);
    for (auto& slot : rtSlots)
        slot.store (-1, std::memory_order_release);

    if (service == nullptr)
        return;

    for (auto& handle : handles)
        if (handle.isValid())
            service->unregisterVirtualChannel (handle);  // invalidiert den Handle
}

//==============================================================================
juce::Result CaptureTapModule::prepareForGraph (double sampleRate, int maximumBlockSize)
{
    if (const auto result = UtilityModule::prepareForGraph (sampleRate, maximumBlockSize);
        result.failed())
        return result;

    // Idempotent (Kontrakt 5.2 Schritt 1): bereits registrierte Kanäle behalten
    if (service == nullptr || isTapRegistered())
        return juce::Result::ok();

    for (size_t ch = 0; ch < handles.size(); ++ch)
    {
        handles[ch] = service->registerVirtualChannel (
            channelNameFor (currentModuleId, static_cast<int> (ch)));

        if (! handles[ch].isValid())
        {
            unregisterChannels();
            return juce::Result::fail (juce::String::fromUTF8 (
                "Keine freien Capture-Tap-Kan\xc3\xa4le (max. ")
                + juce::String (CaptureService::MAX_VIRTUAL_CHANNELS) + ")");
        }
    }

    for (size_t ch = 0; ch < handles.size(); ++ch)
        rtSlots[ch].store (handles[ch].slot, std::memory_order_release);

    rtService.store (service, std::memory_order_release);
    return juce::Result::ok();
}

void CaptureTapModule::prepareToPlay (double, int)
{
    // Keine eigenen Puffer — Meter/Gate/Ring leben beim CaptureService
}

void CaptureTapModule::releaseResources()
{
}

//==============================================================================
void CaptureTapModule::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Pass-Through: der Buffer bleibt unangetastet — nur lesend abgreifen
    auto* capture = rtService.load (std::memory_order_acquire);
    if (capture == nullptr)
        return;

    const auto numSamples = buffer.getNumSamples();
    const auto channels = juce::jmin (numTapChannels, buffer.getNumChannels());

    for (int ch = 0; ch < channels; ++ch)
        capture->writeVirtualChannel (
            { rtSlots[static_cast<size_t> (ch)].load (std::memory_order_acquire) },
            buffer.getReadPointer (ch), numSamples);
}

} // namespace conduit
