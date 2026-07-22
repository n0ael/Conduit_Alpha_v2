#include "TouchProbeComponent.h"

#include "WindowsTouchSuppression.h"

namespace touchlab
{

TouchProbeComponent::TouchProbeComponent()
{
    addAndMakeVisible (traceView);
    addAndMakeVisible (nativeOverlay);  // über der TraceView — fängt Native-Input
    addAndMakeVisible (metricsPanel);
    addChildComponent (blind);          // nur im Blind-Modus sichtbar, über dem Overlay

    title.setText (juce::String::fromUTF8 ("Conduit Touch Lab — Nativ vs. Raw-Pointer"),
                   juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
    addAndMakeVisible (title);

    auto setupSlider = [this] (juce::Slider& s, juce::Label& l, const juce::String& name,
                               double lo, double hi, double interval, double def, int decimals)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        s.setRange (lo, hi, interval);
        s.setNumDecimalPlacesToDisplay (decimals);
        s.setValue (def, juce::dontSendNotification);
        s.onValueChange = [this] { applyParams(); };
        addAndMakeVisible (s);

        l.setText (name, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (13.0f)));
        addAndMakeVisible (l);
    };

    setupSlider (deadZoneSlider,  deadZoneLabel,  "Dead-Zone px",  0.0, 40.0, 0.5,  0.0, 1);
    setupSlider (minCutoffSlider, minCutoffLabel, "One-Euro fc",   0.1, 10.0, 0.1,  1.0, 2);
    setupSlider (betaSlider,      betaLabel,      "One-Euro beta", 0.0, 0.05, 0.001, 0.007, 3);
    setupSlider (jitterSlider,    jitterLabel,    "Jitter-Gate",   0.0, 40.0, 0.5,  8.0, 1);
    setupSlider (sensSlider,      sensLabel,      "Sensitivität",  0.25, 4.0, 0.05, 1.0, 2);

    auto setupToggle = [this] (juce::ToggleButton& t, const juce::String& name, bool on)
    {
        t.setButtonText (name);
        t.setToggleState (on, juce::dontSendNotification);
        t.onClick = [this] { applyParams(); };
        addAndMakeVisible (t);
    };

    setupToggle (deadZoneToggle, "Dead-Zone an",   false);
    setupToggle (oneEuroToggle,  "One-Euro an",    true);
    setupToggle (jitterToggle,   "Jitter-Gate an", false);
    setupToggle (sensToggle,     juce::String::fromUTF8 ("Sensitivität an"), false);

    suppressionToggle.setButtonText (juce::String::fromUTF8 ("Touch-Suppression (Nativ-Parit\xC3\xA4t)"));
    suppressionToggle.setToggleState (true, juce::dontSendNotification);
    suppressionToggle.onClick = [this]
    {
        if (suppressionToggle.getToggleState())
            if (auto* top = getTopLevelComponent())
                applyTouchSuppression (*top);
    };
    addAndMakeVisible (suppressionToggle);

    forcePointerToggle.setButtonText (juce::String::fromUTF8 ("WM_TOUCH abmelden (falls Raw leer)"));
    forcePointerToggle.onClick = [this]
    {
        if (rawSource != nullptr)
            rawSource->setForcePointerMode (forcePointerToggle.getToggleState());
    };
    addAndMakeVisible (forcePointerToggle);

    blindToggle.setButtonText ("Blindvergleich");
    blindToggle.onClick = [this]
    {
        const bool on = blindToggle.getToggleState();
        blind.setActive (on);
        traceView.setBlindMode (on);
    };
    addAndMakeVisible (blindToggle);

    clearButton.setButtonText (juce::String::fromUTF8 ("Spuren l\xC3\xB6schen"));
    clearButton.onClick = [this]
    {
        traceView.clearTrails();
        metricsPanel.resetAll();
    };
    addAndMakeVisible (clearButton);

    applyParams();
    setSize (1100, 720);
}

void TouchProbeComponent::attachRawSource()
{
    if (auto* top = getTopLevelComponent())
    {
        rawSource = std::make_unique<RawPointerSource> (*top, nativeOverlay, *this);
        rawSource->attach();
        rawSource->setForcePointerMode (forcePointerToggle.getToggleState());
    }
}

void TouchProbeComponent::applyParams()
{
    TouchFilterChain::Params p;
    p.deadZoneEnabled  = deadZoneToggle.getToggleState();
    p.deadZoneRadiusPx = (float) deadZoneSlider.getValue();
    p.oneEuroEnabled   = oneEuroToggle.getToggleState();
    p.minCutoff        = (float) minCutoffSlider.getValue();
    p.beta             = (float) betaSlider.getValue();
    p.jitterGateEnabled = jitterToggle.getToggleState();
    p.jitterGateSpeedPxPerSec = (float) jitterSlider.getValue();
    p.sensitivityEnabled = sensToggle.getToggleState();
    p.sensitivity        = (float) sensSlider.getValue();

    nativeFilter.params = p;
    rawFilter.params = p;
}

void TouchProbeComponent::pushSample (const TouchSample& s)
{
    const bool isNative = (s.tag == SourceTag::Native);

    // Raw-Pointer deckt das ganze Fenster ab — Punkte außerhalb der
    // Trace-Fläche (z. B. über den Reglern) verwerfen.
    if (! isNative)
        if (s.x < 0.0f || s.y < 0.0f
            || s.x > (float) traceView.getWidth() || s.y > (float) traceView.getHeight())
            return;

    const bool down = (s.phase == Phase::Down);

    const auto rawLane  = isNative ? TraceView::Lane::NativeRaw : TraceView::Lane::RawRaw;
    const auto filtLane = isNative ? TraceView::Lane::NativeFiltered : TraceView::Lane::RawFiltered;
    const auto mRaw  = isNative ? MetricsPanel::NativeRaw : MetricsPanel::RawRaw;
    const auto mFilt = isNative ? MetricsPanel::NativeFiltered : MetricsPanel::RawFiltered;

    traceView.addPoint (rawLane, { s.x, s.y }, down);
    metricsPanel.record (mRaw, s);

    const auto f = (isNative ? nativeFilter : rawFilter).process (s);
    traceView.addPoint (filtLane, { f.x, f.y }, down);
    metricsPanel.record (mFilt, f);

    blind.consume (s.tag, { f.x, f.y }, s.phase);
}

void TouchProbeComponent::tick()
{
    traceView.refreshIfDirty();
    metricsPanel.refresh();
}

//==============================================================================
void TouchProbeComponent::addRow (juce::Rectangle<int>& area, juce::Label& label,
                                  juce::Component& control)
{
    auto row = area.removeFromTop (26);
    label.setBounds (row.removeFromLeft (120));
    control.setBounds (row.reduced (0, 2));
    area.removeFromTop (6);
}

void TouchProbeComponent::resized()
{
    auto r = getLocalBounds();

    // rechte Regler-Spalte
    auto controls = r.removeFromRight (320).reduced (12);
    title.setBounds (controls.removeFromTop (26));
    controls.removeFromTop (10);

    deadZoneToggle.setBounds (controls.removeFromTop (24));
    addRow (controls, deadZoneLabel, deadZoneSlider);
    oneEuroToggle.setBounds (controls.removeFromTop (24));
    addRow (controls, minCutoffLabel, minCutoffSlider);
    addRow (controls, betaLabel, betaSlider);
    jitterToggle.setBounds (controls.removeFromTop (24));
    addRow (controls, jitterLabel, jitterSlider);
    sensToggle.setBounds (controls.removeFromTop (24));
    addRow (controls, sensLabel, sensSlider);

    controls.removeFromTop (12);
    suppressionToggle.setBounds (controls.removeFromTop (24));
    controls.removeFromTop (4);
    forcePointerToggle.setBounds (controls.removeFromTop (24));
    controls.removeFromTop (4);
    blindToggle.setBounds (controls.removeFromTop (24));
    controls.removeFromTop (10);
    clearButton.setBounds (controls.removeFromTop (30));

    // unten Messwerte, darüber die Trace-Fläche
    auto metrics = r.removeFromBottom (140);
    metricsPanel.setBounds (metrics);

    traceView.setBounds (r);
    nativeOverlay.setBounds (r);   // deckungsgleich mit der TraceView
    blind.setBounds (r);           // deckungsgleich, nur im Blind-Modus sichtbar
}

} // namespace touchlab
