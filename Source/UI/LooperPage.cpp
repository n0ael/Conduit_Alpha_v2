#include "LooperPage.h"

namespace conduit
{

namespace
{
    constexpr int headerHeight = 44;
    constexpr int statusHeight = 22;
    constexpr int panelGap = 6;
}

LooperPage::LooperPage()
{
    setName ("looperPage");

    removeTile.onClick = [this] { if (onRemoveLooper) onRemoveLooper(); };
    addTile.onClick = [this] { if (onAddLooper) onAddLooper(); };
    settingsTile.onClick = [this] { if (onOpenSettings) onOpenSettings(); };
    stopTile.onClick = [this] { if (onStop) onStop(); };

    spectrumTile.onClick = [this]
    {
        if (onViewToggled != nullptr)
            onViewToggled (! spectrumTile.isActive());
    };

    outputCaption.setText ("Output", juce::dontSendNotification);
    outputCaption.setColour (juce::Label::textColourId, push::colours::textDim);
    outputCaption.setJustificationType (juce::Justification::centredRight);

    outputCombo.setName ("looperOutput");
    outputCombo.onChange = [this]
    {
        // Item-ID 1 = „Kein Master-Out" (pairIndex −1, ADR 010), Paare
        // folgen ab ID 2 — Mapping pairIndex = ID − 2
        if (onOutputPairSelected != nullptr && outputCombo.getSelectedId() > 0)
            onOutputPairSelected (outputCombo.getSelectedId() - 2);
    };

    statusLabel.setColour (juce::Label::textColourId, push::colours::textDim);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (removeTile);
    addAndMakeVisible (addTile);
    addAndMakeVisible (outputCaption);
    addAndMakeVisible (outputCombo);
    addAndMakeVisible (spectrumTile);
    addAndMakeVisible (settingsTile);
    addAndMakeVisible (stopTile);
    addAndMakeVisible (statusLabel);

    setLooperCount (1);
}

void LooperPage::setLooperCount (int count)
{
    count = juce::jlimit (1, 4, count);
    if ((int) panels.size() == count)
        return;

    while ((int) panels.size() > count)
        panels.pop_back();

    while ((int) panels.size() < count)
    {
        auto panel = std::make_unique<LooperPanel> ((int) panels.size() + 1);
        addAndMakeVisible (*panel);
        panels.push_back (std::move (panel));
    }

    removeTile.setAlpha (count > 1 ? 1.0f : 0.4f);
    addTile.setAlpha (count < 4 ? 1.0f : 0.4f);

    resized();

    if (onPanelsChanged != nullptr)
        onPanelsChanged();
}

LooperPanel& LooperPage::getPanel (int looperIndex)
{
    jassert (looperIndex >= 0 && looperIndex < (int) panels.size());
    return *panels[(size_t) juce::jlimit (0, (int) panels.size() - 1, looperIndex)];
}

void LooperPage::setOutputPairs (const juce::StringArray& pairLabels, int selectedPair)
{
    outputCombo.clear (juce::dontSendNotification);

    // „Kein Master-Out" (pairIndex −1, ADR 010) immer zuoberst — die
    // Looper laufen weiter, nur die Anker-Ausgabe entfällt
    outputCombo.addItem ("Kein Master-Out", 1);
    for (int i = 0; i < pairLabels.size(); ++i)
        outputCombo.addItem (pairLabels[i], i + 2);

    outputCombo.setSelectedId (juce::jlimit (-1, pairLabels.size() - 1, selectedPair) + 2,
                               juce::dontSendNotification);
}

void LooperPage::setSpectrumView (bool spectrum)
{
    spectrumTile.setActive (spectrum);
    for (auto& panel : panels)
        panel->getStrip().setView (spectrum ? LooperWaveformStrip::View::spectrum
                                            : LooperWaveformStrip::View::waveform);
}

void LooperPage::setStatus (const juce::String& statusText)
{
    statusLabel.setText (statusText, juce::dontSendNotification);
}

void LooperPage::setPulsePhase (float phase01)
{
    for (auto& panel : panels)
        panel->setPulsePhase (phase01);
}

void LooperPage::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::background);

    g.setColour (push::colours::text);
    g.setFont (push::scaledFont (15.0f, true));
    g.drawText ("FFT LOOPER",
                getLocalBounds().removeFromTop (headerHeight).reduced (10, 0),
                juce::Justification::centredLeft, false);
}

void LooperPage::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (headerHeight).reduced (4);
    header.removeFromLeft (110);   // Titel (paint)
    removeTile.setBounds (header.removeFromLeft (36).reduced (2));
    addTile.setBounds (header.removeFromLeft (36).reduced (2));

    stopTile.setBounds (header.removeFromRight (72).reduced (2));
    spectrumTile.setBounds (header.removeFromRight (92).reduced (2));
    settingsTile.setBounds (header.removeFromRight (44).reduced (2));
    outputCombo.setBounds (header.removeFromRight (190).reduced (2, 6));
    outputCaption.setBounds (header.removeFromRight (60));

    statusLabel.setBounds (bounds.removeFromBottom (statusHeight).reduced (10, 0));

    auto panelArea = bounds.reduced (4);
    const auto count = (int) panels.size();
    if (count > 0)
    {
        const auto panelWidth = (panelArea.getWidth() - panelGap * (count - 1)) / count;
        for (int i = 0; i < count; ++i)
        {
            panels[(size_t) i]->setBounds (panelArea.removeFromLeft (panelWidth));
            panelArea.removeFromLeft (panelGap);
        }
    }
}

} // namespace conduit
