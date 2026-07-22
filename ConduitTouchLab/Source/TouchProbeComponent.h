#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "BlindAbMode.h"
#include "FramePacer.h"
#include "MetricsPanel.h"
#include "NativeTouchSource.h"
#include "RawPointerSource.h"
#include "TouchFilterChain.h"
#include "TouchSample.h"
#include "TraceView.h"

namespace touchlab
{

//==============================================================================
/**
    Der Hub: implementiert TouchSink, verteilt jedes Sample an TraceView,
    MetricsPanel und (im Blind-Modus) BlindAbMode — roh UND durch die
    Filterkette. Hält die Live-Regler (Form-Muster Conduit
    UiSettingsComponent) und den gemeinsamen FramePacer.
*/
class TouchProbeComponent final : public juce::Component,
                                  public TouchSink
{
public:
    TouchProbeComponent();

    /** Nach setVisible vom Fenster gerufen: HWND existiert erst dann. */
    void attachRawSource();

    void pushSample (const TouchSample& sample) override;

    void resized() override;

private:
    void applyParams();
    void tick();
    void addRow (juce::Rectangle<int>& area, juce::Label& label, juce::Component& control);

    // Datenpfade
    TraceView traceView;
    NativeTouchSource nativeOverlay { *this };
    MetricsPanel metricsPanel;
    BlindAbMode blind;
    TouchFilterChain nativeFilter, rawFilter;
    std::unique_ptr<RawPointerSource> rawSource;

    // Regler
    juce::Label      title;
    juce::ToggleButton deadZoneToggle, oneEuroToggle, jitterToggle, sensToggle;
    juce::Slider     deadZoneSlider, minCutoffSlider, betaSlider, jitterSlider, sensSlider;
    juce::Label      deadZoneLabel, minCutoffLabel, betaLabel, jitterLabel, sensLabel;
    juce::ToggleButton suppressionToggle, forcePointerToggle, blindToggle;
    juce::TextButton clearButton { "clear" };  // Text via fromUTF8 im Ctor

    FramePacer pacer { this, [this] { tick(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchProbeComponent)
};

} // namespace touchlab
