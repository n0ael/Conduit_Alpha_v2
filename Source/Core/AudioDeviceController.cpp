#include "AudioDeviceController.h"

#include "EngineProcessor.h"

namespace conduit
{

namespace
{
    // Schlüssel des Device-State-XML in der PropertiesFile
    constexpr const char* deviceStateKey = "audioDeviceState";
}

//==============================================================================
juce::PropertiesFile::Options AudioDeviceController::defaultOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "AudioDevice";
    options.filenameSuffix      = ".settings";
    options.folderName          = "Conduit";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

//==============================================================================
AudioDeviceController::AudioDeviceController (const juce::PropertiesFile::Options& options)
{
    applicationProperties.setStorageParameters (options);
}

AudioDeviceController::~AudioDeviceController()
{
    shutdown();
}

//==============================================================================
juce::String AudioDeviceController::computeWarning (double sampleRate, int bufferSize)
{
    // Latenz-Ziele (CLAUDE.md 3.2): 48 kHz / 32 Samples, Fallback 44.1 kHz / 64
    const bool rateOk   = juce::approximatelyEqual (sampleRate, 48000.0)
                       || juce::approximatelyEqual (sampleRate, 44100.0);
    const bool bufferOk = bufferSize <= 64;

    if (rateOk && bufferOk)
        return {};

    return juce::String (sampleRate, 0) + " Hz / "
               + juce::String (bufferSize) + " Samples (Ziel: 48000 Hz / 32)";
}

//==============================================================================
void AudioDeviceController::initialise (EngineProcessor& engineToUse)
{
    JUCE_ASSERT_MESSAGE_THREAD
    engine = &engineToUse;

    // Persistenz: gespeicherten Device-Zustand wiederherstellen. Nur beim
    // Erststart (kein gespeicherter Zustand) forcieren wir 48k/32.
    std::unique_ptr<juce::XmlElement> savedState;
    if (auto* file = applicationProperties.getUserSettings())
        savedState = file->getXmlValue (deviceStateKey);

    if (savedState != nullptr)
    {
        deviceManager.initialise (0, 32, savedState.get(), true);
    }
    else
    {
        auto initError = deviceManager.initialiseWithDefaultDevices (2, 2);

        // Kein Input-Device (kein Mikrofon/Interface) → Output-only-Fallback:
        // Conduit braucht primär Ausgänge, Eingänge sind optional (9.1).
        if (deviceManager.getCurrentAudioDevice() == nullptr)
            initError = deviceManager.initialiseWithDefaultDevices (0, 2);

        auto setup = deviceManager.getAudioDeviceSetup();
        setup.sampleRate = 48000.0;
        setup.bufferSize = 32;
        deviceManager.setAudioDeviceSetup (setup, true);

        juce::ignoreUnused (initError);
    }

    player.setProcessor (&engineToUse);
    deviceManager.addAudioCallback (&player);
    deviceManager.addChangeListener (this);

    applyActiveDevice();
}

void AudioDeviceController::shutdown()
{
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (&player);
    player.setProcessor (nullptr);
    engine = nullptr;
}

//==============================================================================
void AudioDeviceController::changeListenerCallback (juce::ChangeBroadcaster*)
{
    applyActiveDevice();
}

void AudioDeviceController::applyActiveDevice()
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (engine == nullptr)
        return;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        // Aktive Kanal-Auswahl aus dem Audio-Setup — Port/Prozessor-Kanäle sind
        // die komprimierten aktiven Kanäle (AudioProcessorPlayer)
        const auto activeInputs  = device->getActiveInputChannels();
        const auto activeOutputs = device->getActiveOutputChannels();

        // Kanal-Namen-Kontext: Device-Name als Key, gemeldete Kanalnamen als
        // Default-Labels; die Auswahl-Masken sorgen für das korrekte Port →
        // Geräte-Kanal-Mapping (ChannelNames-Doku)
        engine->getChannelNames().setActiveDevice (device->getName(),
                                                   device->getInputChannelNames(),
                                                   device->getOutputChannelNames(),
                                                   activeInputs,
                                                   activeOutputs);

        // Echte Hardware-Kanalzahl an die I/O-Tree-Nodes koppeln (Schritt B).
        // Aktive Kanäle wie der AudioProcessorPlayer (findMostSuitableLayout →
        // deviceChannels), damit Port-UI und Graph dieselbe Zahl tragen.
        engine->syncHardwareIOChannels (activeInputs.countNumberOfSetBits(),
                                        activeOutputs.countNumberOfSetBits());

        const auto warning = computeWarning (device->getCurrentSampleRate(),
                                             device->getCurrentBufferSizeSamples());

        if (warning.isNotEmpty())
            engine->getRootState().setProperty (id::audioSetupWarning, warning, nullptr);
        else
            engine->getRootState().removeProperty (id::audioSetupWarning, nullptr);
    }
    else
    {
        engine->getRootState().setProperty (id::audioSetupWarning,
                                            "Kein Audio-Device verfügbar", nullptr);
    }

    saveDeviceState();
}

void AudioDeviceController::saveDeviceState()
{
    if (auto* file = applicationProperties.getUserSettings())
    {
        if (const auto state = deviceManager.createStateXml())
            file->setValue (deviceStateKey, state.get());
        else
            file->removeValue (deviceStateKey);

        file->saveIfNeeded();
    }
}

} // namespace conduit
