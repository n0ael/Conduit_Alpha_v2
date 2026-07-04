#include "PageHost.h"

#include "PushLookAndFeel.h"
#include "TransportBar.h"

namespace conduit
{

PageHost::PageHost (juce::Component& devicePage, juce::Component& looperPage)
    : device (devicePage), looper (looperPage)
{
    addChildComponent (gridPage);
    addChildComponent (mixerPage);
    addChildComponent (clipPage);
    addChildComponent (looper);
    addAndMakeVisible (device);

    setPage (currentPage);
}

void PageHost::setPage (int pageIndex)
{
    currentPage = juce::jlimit (0, (int) TransportBar::pageLooper, pageIndex);

    gridPage.setVisible (currentPage == TransportBar::pageGrid);
    mixerPage.setVisible (currentPage == TransportBar::pageMixer);
    clipPage.setVisible (currentPage == TransportBar::pageClip);
    device.setVisible (currentPage == TransportBar::pageDevice);
    looper.setVisible (currentPage == TransportBar::pageLooper);
}

void PageHost::resized()
{
    const auto bounds = getLocalBounds();
    gridPage.setBounds (bounds);
    mixerPage.setBounds (bounds);
    clipPage.setBounds (bounds);
    device.setBounds (bounds);
    looper.setBounds (bounds);
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
    g.setFont (push::scaledFont (26.0f));
    g.drawText (title, area.withTop (centre.y + 16.0f).withHeight (34.0f),
                juce::Justification::centredTop);

    g.setColour (push::colours::textDim);
    g.setFont (push::scaledFont (15.0f));
    g.drawText ("kommt als eigener Meilenstein",
                area.withTop (centre.y + 52.0f).withHeight (24.0f),
                juce::Justification::centredTop);
}

} // namespace conduit
