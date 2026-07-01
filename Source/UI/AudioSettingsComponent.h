#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace conduit
{

//==============================================================================
/**
    Container um die native juce::AudioDeviceSelectorComponent (Treiber-Typ,
    Device, Samplerate, Buffer, Kanalauswahl — automatisch systemabhängig).

    Bewusst als eigener Wrapper: das AudioDeviceManager-Backend ist vom
    Frontend entkoppelt, ein späterer Umstieg auf handgebaute Combos ändert
    hier nur die Innerei, nicht die Verdrahtung/Persistenz. Optik über ein
    LookAndFeel_V4 (Midnight-Scheme) an den Conduit-Dark-Look angeglichen.

    Wird non-modal in einem juce::DialogWindow gezeigt (EngineEditor).
*/
class AudioSettingsComponent final : public juce::Component
{
public:
    explicit AudioSettingsComponent (juce::AudioDeviceManager& deviceManager);
    ~AudioSettingsComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::LookAndFeel_V4 darkLook { juce::LookAndFeel_V4::getMidnightColourScheme() };

    juce::Label headerLabel;
    juce::AudioDeviceSelectorComponent selector;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsComponent)
};

} // namespace conduit
