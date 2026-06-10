#include <juce_audio_utils/juce_audio_utils.h>

#include "EngineEditor.h"
#include "EngineProcessor.h"

namespace conduit
{

//==============================================================================
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, EngineProcessor& engine)
        : juce::DocumentWindow (name,
                                juce::Desktop::getInstance().getDefaultLookAndFeel()
                                    .findColour (juce::ResizableWindow::backgroundColourId),
                                juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        // Ownership geht an das DocumentWindow (JUCE-API verlangt raw new hier)
        setContentOwned (new EngineEditor (engine), true);
        setResizable (true, true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

//==============================================================================
class ConduitApplication final : public juce::JUCEApplication
{
public:
    ConduitApplication() = default;

    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        engine = std::make_unique<EngineProcessor>();
        initAudio();
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine);
    }

    void shutdown() override
    {
        mainWindow = nullptr;

        deviceManager.removeAudioCallback (&player);
        player.setProcessor (nullptr);
        engine = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    //==========================================================================
    // Plattformspezifisches Audio-Setup ist hier explizit erlaubt (CLAUDE.md 9).
    // Latenz-Ziele: 48 kHz / 32 Samples, Fallback 44.1 kHz / 64 Samples (3.2).
    void initAudio()
    {
        const auto initError = deviceManager.initialiseWithDefaultDevices (2, 2);

        auto setup = deviceManager.getAudioDeviceSetup();
        setup.sampleRate = 48000.0;
        setup.bufferSize = 32;
        deviceManager.setAudioDeviceSetup (setup, true);

        // Defensiv: Hardware kann abweichende Werte erzwingen — kein Crash,
        // Abweichung in ValueTree-Property audioSetupWarning (CLAUDE.md 9.1).
        if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            const auto actualRate   = device->getCurrentSampleRate();
            const auto actualBuffer = device->getCurrentBufferSizeSamples();

            const bool rateOk   = juce::approximatelyEqual (actualRate, 48000.0)
                               || juce::approximatelyEqual (actualRate, 44100.0);
            const bool bufferOk = actualBuffer <= 64;

            if (! rateOk || ! bufferOk)
                engine->getRootState().setProperty (id::audioSetupWarning,
                    juce::String (actualRate, 0) + " Hz / "
                        + juce::String (actualBuffer) + " Samples (Ziel: 48000 Hz / 32)",
                    nullptr);
        }
        else
        {
            engine->getRootState().setProperty (id::audioSetupWarning,
                initError.isNotEmpty() ? initError : juce::String ("Kein Audio-Device verfügbar"),
                nullptr);
        }

        player.setProcessor (engine.get());
        deviceManager.addAudioCallback (&player);
    }

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer player;

    std::unique_ptr<EngineProcessor> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace conduit

//==============================================================================
START_JUCE_APPLICATION (conduit::ConduitApplication)
