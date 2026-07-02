#include "PageHost.h"

#include "PushLookAndFeel.h"

namespace conduit
{

PageHost::PageHost (juce::Component& devicePage)
    : device (devicePage)
{
    addChildComponent (gridPage);
    addChildComponent (mixerPage);
    addChildComponent (clipPage);
    addAndMakeVisible (device);

    setPage (currentPage);
}

void PageHost::setPage (int pageIndex)
{
    currentPage = juce::jlimit (0, 3, pageIndex);

    gridPage.setVisible (currentPage == 0);
    mixerPage.setVisible (currentPage == 1);
    clipPage.setVisible (currentPage == 2);
    device.setVisible (currentPage == 3);
}

void PageHost::resized()
{
    const auto bounds = getLocalBounds();
    gridPage.setBounds (bounds);
    mixerPage.setBounds (bounds);
    clipPage.setBounds (bounds);
    device.setBounds (bounds);
}

//==============================================================================
PageHost::Placeholder::Placeholder (push::Icon iconToUse, juce::String titleToUse)
    : icon (iconToUse), title (std::move (titleToUse))
{
}

void PageHost::Placeholder::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::background);

    auto area = getLocalBounds().toFloat();
    const auto centre = area.getCentre();

    push::draw (g, icon, juce::Rectangle<float> (72.0f, 72.0f).withCentre (centre.translated (0.0f, -40.0f)),
                push::colours::textDim);

    g.setColour (push::colours::text);
    g.setFont (juce::Font (juce::FontOptions {}.withHeight (26.0f)));
    g.drawText (title, area.withTop (centre.y + 16.0f).withHeight (34.0f),
                juce::Justification::centredTop);

    g.setColour (push::colours::textDim);
    g.setFont (juce::Font (juce::FontOptions {}.withHeight (15.0f)));
    g.drawText ("kommt als eigener Meilenstein",
                area.withTop (centre.y + 52.0f).withHeight (24.0f),
                juce::Justification::centredTop);
}

} // namespace conduit
