#include <juce_audio_utils/juce_audio_utils.h>

#include "AudioDeviceController.h"
#include "EngineEditor.h"
#include "EngineProcessor.h"
#include "UI/PerformanceWindowSetup.h"
#include "UI/PushLookAndFeel.h"

namespace conduit
{

//==============================================================================
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, EngineProcessor& engine,
                juce::AudioDeviceManager& deviceManager)
        : juce::DocumentWindow (name,
                                juce::Desktop::getInstance().getDefaultLookAndFeel()
                                    .findColour (juce::ResizableWindow::backgroundColourId),
                                juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        // Ownership geht an das DocumentWindow (JUCE-API verlangt raw new hier)
        setContentOwned (new EngineEditor (engine, &deviceManager), true);
        setResizable (true, true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);

        // Performance-Modus-Grundlagen (ADR 008 M4): Touch-Systemgesten
        // unterdrücken — nach setVisible (der native Peer muss existieren)
        applyPerformanceTouchSetup (*this);
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

        // Persistierte Oberflächen-Skalierung VOR der Fenster-Erzeugung
        // anwenden (Ableton-Verhalten: skaliert die gesamte App; Faktor
        // multipliziert sich auf das OS-Display-Scaling). Schriftgröße
        // ebenso — die LnF-Fonts lesen den Faktor beim Zeichnen.
        juce::Desktop::getInstance().setGlobalScaleFactor (
            engine->getUiSettings().getUiScale());
        push::setFontScale (engine->getUiSettings().getFontScale());

        // Plattformspezifisches Audio-Setup gehört in die App (CLAUDE.md 9);
        // der Controller kapselt Device-Manager, Player, Persistenz und die
        // Live-Anwendung von ChannelNames/audioSetupWarning.
        audioController.initialise (*engine);

        // OSC-Empfang aktivieren (CLAUDE.md 7) — Status zeigt die Toolbar
        if (! engine->getOscController().connect (OscController::defaultPort))
            juce::Logger::writeToLog ("OSC: Port " + juce::String (OscController::defaultPort)
                                      + " belegt — Empfang deaktiviert");

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine,
                                                   audioController.getDeviceManager());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        audioController.shutdown();
        engine = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    AudioDeviceController audioController;

    std::unique_ptr<EngineProcessor> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace conduit

//==============================================================================
START_JUCE_APPLICATION (conduit::ConduitApplication)
