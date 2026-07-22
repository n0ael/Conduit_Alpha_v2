#include <juce_gui_extra/juce_gui_extra.h>

#include "TouchProbeComponent.h"
#include "WindowsTouchSuppression.h"

namespace touchlab
{

//==============================================================================
class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow (const juce::String& name)
        : juce::DocumentWindow (name,
                                juce::Desktop::getInstance().getDefaultLookAndFeel()
                                    .findColour (juce::ResizableWindow::backgroundColourId),
                                juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        probe = new TouchProbeComponent();
        setContentOwned (probe, true);   // Ownership geht ans DocumentWindow
        setResizable (true, true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);

        // Erst nach setVisible: der native Peer/HWND muss existieren
        // (analog Conduit Main.cpp -> applyPerformanceTouchSetup).
        applyTouchSuppression (*this);
        probe->attachRawSource();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    TouchProbeComponent* probe = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

//==============================================================================
class TouchLabApplication final : public juce::JUCEApplication
{
public:
    TouchLabApplication() = default;

    const juce::String getApplicationName() override    { return juce::String::fromUTF8 ("Conduit Touch Lab"); }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace touchlab

//==============================================================================
START_JUCE_APPLICATION (touchlab::TouchLabApplication)
