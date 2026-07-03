#include "DevPanel.h"

#include "UI/PushLookAndFeel.h"
#include "UI/UiSettingsComponent.h"

namespace conduit
{

DevPanel::DevPanel (UiSettings& uiSettingsToUse)
    : juce::DocumentWindow ("Dev", push::colours::panel,
                            juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setAlwaysOnTop (true);

    auto content = std::make_unique<UiSettingsComponent> (uiSettingsToUse);
    content->setSize (380, UiSettingsComponent::preferredHeight());
    setContentOwned (content.release(), true);

    setResizable (false, false);
    setVisible (true);
}

void DevPanel::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();
}

} // namespace conduit
