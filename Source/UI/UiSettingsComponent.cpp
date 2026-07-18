#include "UiSettingsComponent.h"

namespace conduit
{

UiSettingsComponent::UiSettingsComponent (UiSettings& settingsToUse)
    : settings (settingsToUse)
{
    header.setText (juce::String::fromUTF8 ("Oberfläche"), juce::dontSendNotification);
    header.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    addAndMakeVisible (header);

    // UI-Größe: 1%-Schritte wie Ableton; Commit erst am Drag-Ende — das
    // Fenster skaliert unter dem Slider weg (setGlobalScaleFactor trifft
    // ALLE Fenster), kontinuierliches Anwenden wäre eine Feedback-Schleife
    addAndMakeVisible (uiScaleLabel);
    uiScaleSlider.setRange (static_cast<double> (UiSettings::minUiScale) * 100.0,
                            static_cast<double> (UiSettings::maxUiScale) * 100.0, 1.0);
    uiScaleSlider.setTextValueSuffix (" %");
    uiScaleSlider.onDragEnd = [this] { applyUiScale(); };
    uiScaleSlider.onValueChange = [this]
    {
        if (! uiScaleSlider.isMouseButtonDown())   // Bahn-Klick/Pfeile/TextBox
            applyUiScale();
    };
    addAndMakeVisible (uiScaleSlider);

    // Schriftgröße wirkt LIVE beim Ziehen — anders als beim UI-Scale gibt es
    // keine Feedback-Schleife (das Fenster skaliert nicht unter dem Cursor,
    // nur die Texte zeichnen neu; Repaints koalesziert der Message-Loop)
    addAndMakeVisible (fontScaleLabel);
    fontScaleSlider.setRange (static_cast<double> (UiSettings::minFontScale) * 100.0,
                              static_cast<double> (UiSettings::maxFontScale) * 100.0, 1.0);
    fontScaleSlider.setTextValueSuffix (" %");
    fontScaleSlider.onValueChange = [this] { applyFontScale(); };
    addAndMakeVisible (fontScaleSlider);

    // UI-Framerate (User-Regel 14.07.2026): Anzeige läuft nativ per VBlank,
    // hier nur die globale Obergrenze — Item-Ids = fps (30/60/120).
    addAndMakeVisible (fpsLimitLabel);
    fpsLimitCombo.addItem ("Nativ (max 120 fps)", 120);
    fpsLimitCombo.addItem ("60 fps", 60);
    fpsLimitCombo.addItem ("30 fps", 30);
    fpsLimitCombo.onChange = [this]
    {
        if (fpsLimitCombo.getSelectedId() > 0)
            settings.setUiFpsLimit (fpsLimitCombo.getSelectedId());
    };
    addAndMakeVisible (fpsLimitCombo);

    // Interaktions-Zoom-Grenze (ADR 008 M3a): unterhalb des Werts sind
    // Node-Module reine Navigationsziele — Dev-Tuning pro Gerät (Prozent)
    addAndMakeVisible (minZoomLabel);
    minZoomSlider.setRange (static_cast<double> (UiSettings::minInteractionMinZoom) * 100.0,
                            static_cast<double> (UiSettings::maxInteractionMinZoom) * 100.0, 1.0);
    minZoomSlider.setTextValueSuffix (" %");
    minZoomSlider.onValueChange = [this]
    { settings.setInteractionMinZoom (static_cast<float> (minZoomSlider.getValue() / 100.0)); };
    addAndMakeVisible (minZoomSlider);

    // Pinch-Schwelle (ADR 008 M3a): Spread-Änderung in %, ab der eine
    // 2-Finger-Bewegung als Zoom zählt — pro Touchscreen justierbar
    // (User-Feedback 18.07.2026: ungenaue Screens zoomen sonst beim Pannen)
    addAndMakeVisible (pinchDeadZoneLabel);
    pinchDeadZoneSlider.setRange (static_cast<double> (UiSettings::minPinchDeadZone) * 100.0,
                                  static_cast<double> (UiSettings::maxPinchDeadZone) * 100.0, 1.0);
    pinchDeadZoneSlider.setTextValueSuffix (" %");
    pinchDeadZoneSlider.onValueChange = [this]
    { settings.setPinchDeadZone (static_cast<float> (pinchDeadZoneSlider.getValue() / 100.0)); };
    addAndMakeVisible (pinchDeadZoneSlider);

    // Zoom-Antwort (ADR 008 M3a): Stärke = Gesamt-Geschwindigkeit des
    // Pinch-Zooms; Kurve > 1.0 = beginnt langsam, wird progressiv stärker
    // (User-Feedback 18.07.2026: linear zoomte zu schnell zu stark)
    addAndMakeVisible (zoomStrengthLabel);
    zoomStrengthSlider.setRange (static_cast<double> (UiSettings::minZoomStrength) * 100.0,
                                 static_cast<double> (UiSettings::maxZoomStrength) * 100.0, 1.0);
    zoomStrengthSlider.setTextValueSuffix (" %");
    zoomStrengthSlider.onValueChange = [this]
    { settings.setZoomStrength (static_cast<float> (zoomStrengthSlider.getValue() / 100.0)); };
    addAndMakeVisible (zoomStrengthSlider);

    addAndMakeVisible (zoomCurveLabel);
    zoomCurveSlider.setRange (static_cast<double> (UiSettings::minZoomCurve),
                              static_cast<double> (UiSettings::maxZoomCurve), 0.1);
    zoomCurveSlider.setNumDecimalPlacesToDisplay (1);
    zoomCurveSlider.onValueChange = [this]
    { settings.setZoomCurve (static_cast<float> (zoomCurveSlider.getValue())); };
    addAndMakeVisible (zoomCurveSlider);

    // Gesten-Glättung (ADR 008 M3a, Release-Smoke 18.07.2026): Tiefpass
    // gegen Sensor-Rauschen — Zittern beim 2-Finger-Pan; 0 % = roh
    addAndMakeVisible (smoothingLabel);
    smoothingSlider.setRange (static_cast<double> (UiSettings::minGestureSmoothing) * 100.0,
                              static_cast<double> (UiSettings::maxGestureSmoothing) * 100.0, 1.0);
    smoothingSlider.setTextValueSuffix (" %");
    smoothingSlider.onValueChange = [this]
    { settings.setGestureSmoothing (static_cast<float> (smoothingSlider.getValue() / 100.0)); };
    addAndMakeVisible (smoothingSlider);

    devModeToggle.setButtonText (juce::String::fromUTF8 (
        "Development-Modus (DEV-Buttons in den Modul-Köpfen)"));
    devModeToggle.onClick = [this] { settings.setDevModeEnabled (devModeToggle.getToggleState()); };
    addAndMakeVisible (devModeToggle);

    dspMeterToggle.setButtonText (juce::String::fromUTF8 (
        "DSP-Meter im Transport (Load ⌀/Peak + XRuns)"));
    dspMeterToggle.onClick = [this] { settings.setDspMeterEnabled (dspMeterToggle.getToggleState()); };
    addAndMakeVisible (dspMeterToggle);

    softKeyboardToggle.setButtonText (juce::String::fromUTF8 (
        "On-Screen-Tastatur im Browser-Suchfeld (Touch)"));
    softKeyboardToggle.onClick = [this]
    { settings.setSoftKeyboardEnabled (softKeyboardToggle.getToggleState()); };
    addAndMakeVisible (softKeyboardToggle);

    settings.addChangeListener (this);
    syncControls();
}

UiSettingsComponent::~UiSettingsComponent()
{
    settings.removeChangeListener (this);
}

//==============================================================================
void UiSettingsComponent::applyUiScale()
{
    settings.setUiScale (static_cast<float> (uiScaleSlider.getValue() / 100.0));
}

void UiSettingsComponent::applyFontScale()
{
    settings.setFontScale (static_cast<float> (fontScaleSlider.getValue() / 100.0));
}

void UiSettingsComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    syncControls();   // Settings → Controls (auch vom Dev-Panel/Tab-Zwilling)
}

void UiSettingsComponent::syncControls()
{
    uiScaleSlider.setValue (static_cast<double> (settings.getUiScale()) * 100.0,
                            juce::dontSendNotification);
    fontScaleSlider.setValue (static_cast<double> (settings.getFontScale()) * 100.0,
                              juce::dontSendNotification);
    fpsLimitCombo.setSelectedId (settings.getUiFpsLimit(), juce::dontSendNotification);
    minZoomSlider.setValue (static_cast<double> (settings.getInteractionMinZoom()) * 100.0,
                            juce::dontSendNotification);
    pinchDeadZoneSlider.setValue (static_cast<double> (settings.getPinchDeadZone()) * 100.0,
                                  juce::dontSendNotification);
    zoomStrengthSlider.setValue (static_cast<double> (settings.getZoomStrength()) * 100.0,
                                 juce::dontSendNotification);
    zoomCurveSlider.setValue (static_cast<double> (settings.getZoomCurve()),
                              juce::dontSendNotification);
    smoothingSlider.setValue (static_cast<double> (settings.getGestureSmoothing()) * 100.0,
                              juce::dontSendNotification);
    devModeToggle.setToggleState (settings.isDevModeEnabled(), juce::dontSendNotification);
    dspMeterToggle.setToggleState (settings.isDspMeterEnabled(), juce::dontSendNotification);
    softKeyboardToggle.setToggleState (settings.isSoftKeyboardEnabled(),
                                       juce::dontSendNotification);
}

//==============================================================================
void UiSettingsComponent::layoutRow (juce::Rectangle<int>& area, juce::Label& label,
                                     juce::Component& control, int rowHeight)
{
    auto row = area.removeFromTop (rowHeight);
    label.setBounds (row.removeFromLeft (110));
    control.setBounds (row.reduced (0, 2));
    area.removeFromTop (6);
}

void UiSettingsComponent::resized()
{
    auto area = getLocalBounds().reduced (18);

    header.setBounds (area.removeFromTop (28));
    area.removeFromTop (8);

    layoutRow (area, uiScaleLabel, uiScaleSlider);
    layoutRow (area, fontScaleLabel, fontScaleSlider);
    layoutRow (area, fpsLimitLabel, fpsLimitCombo);
    layoutRow (area, minZoomLabel, minZoomSlider);
    layoutRow (area, pinchDeadZoneLabel, pinchDeadZoneSlider);
    layoutRow (area, zoomStrengthLabel, zoomStrengthSlider);
    layoutRow (area, zoomCurveLabel, zoomCurveSlider);
    layoutRow (area, smoothingLabel, smoothingSlider);

    devModeToggle.setBounds (area.removeFromTop (30));
    area.removeFromTop (6);
    dspMeterToggle.setBounds (area.removeFromTop (30));
    area.removeFromTop (6);
    softKeyboardToggle.setBounds (area.removeFromTop (30));
}

} // namespace conduit
