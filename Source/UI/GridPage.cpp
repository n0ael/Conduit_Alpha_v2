#include "GridPage.h"

#include <cmath>

#include "CcPanel.h"
#include "GridSettingsView.h"
#include "MacroPanel.h"
#include "MappingsListComponent.h"
#include "Modules/ConduitModule.h"
#include "MpeShapingView.h"
#include "TransportBar.h"
#include "TouchLive/AbletonParamTarget.h"
#include "TouchLive/LiveTargetResolver.h"

namespace conduit
{

GridPage::GridPage (juce::ValueTree rootStateToUse,
                     grid::GridVoiceEngine& engineToUse,
                     GridPanelSettings& panelSettingsToUse, grid::MpeMidiSink& mpeMidiSinkToUse,
                     LiveSetModel& liveSetModelToUse, TouchLiveClient& touchLiveClientToUse,
                     MidiPortHub& midiPortHubToUse, MidiRigSettings& midiRigSettingsToUse,
                     MidiProfileLibrary& midiProfileLibraryToUse,
                     ControllerProfileLibrary& controllerProfileLibraryToUse,
                     EditorDockPanel& dockPanelToUse,
                     IParamModulationSink& paramModSinkToUse,
                     LinkClock& linkClockToUse)
    : rootState (std::move (rootStateToUse)),
      engine (engineToUse),
      midiPortHub (midiPortHubToUse), midiRigSettings (midiRigSettingsToUse),
      midiProfileLibrary (midiProfileLibraryToUse),
      controllerProfileLibrary (controllerProfileLibraryToUse),
      linkClock (linkClockToUse),
      midiTarget (midiPortHubToUse.gridOutputTarget()),
      panelSettings (panelSettingsToUse),
      paramModSink (paramModSinkToUse),
      mpeMidiSink (mpeMidiSinkToUse),
      liveSetModel (liveSetModelToUse), touchLiveClient (touchLiveClientToUse),
      systemControlRowsAtStartup (panelSettingsToUse.getSystemControlRows()),
      trackTabs (liveSetModelToUse, panelSettingsToUse),
      // 8×8-Raster der Grid-Page (padLayoutConfig, User 10.07.2026) — Keyboard
      // und ccLayer teilen sich dieselbe Zellgeometrie.
      keyboard (engineToUse, padLayoutConfig()),
      ccLayer (ccModel, padLayoutConfig().cols, padLayoutConfig().rows),
      // System-Controls des XY+Fader-Modus: eigenes 8×N-Zellraster exakt über
      // den oberen N Pad-Reihen (resized), N = systemControlRowsAtStartup.
      systemLayer (systemCcModel, padLayoutConfig().cols, systemControlRowsAtStartup),
      dockPanel (dockPanelToUse)
{
    addAndMakeVisible (trackTabs);
    addAndMakeVisible (masterSwitch);
    addAndMakeVisible (armButton);
    addAndMakeVisible (releaseAllButton);
    addAndMakeVisible (octaveUpTile);
    addAndMakeVisible (octaveDownTile);
    addAndMakeVisible (atOffsetRibbon);
    addAndMakeVisible (slideOffsetRibbon);
    addAndMakeVisible (pitchOffsetRibbon);
    addChildComponent (modwheelRibbon);   // sichtbar nur bei aktiviertem Modwheel (Block D1)
    addAndMakeVisible (keyboard);
    addAndMakeVisible (ccLayer);     // NACH keyboard hinzugefügt = liegt darüber
    addChildComponent (systemLayer); // NACH ccLayer = darüber: System-Controls
                                     // gewinnen die Hit-Tests auch im CC-Tab-
                                     // Modus (dort SPIELBAR — TODO(design));
                                     // sichtbar nur im XY+Fader-Modus.
    addAndMakeVisible (chordStrip);  // eigene Spalte NEBEN Keyboard/ccLayer
    // M5b: dockPanel ist Kind des EngineEditor (app-weit) — GridPage
    // registriert unten nur seine Tabs (Page-Maske "nur Grid-Page").

    // Block J (Physics): Keyboard (Grid-Gravity/Pitch-Schatten) und beide
    // Control-Layer (Fader/XY-Physics) pollen die Settings live pro Frame
    // (Toggles im Settings-Tab, Feder-Tuning im Dev-Panel).
    keyboard.setPanelSettings (&panelSettings);
    ccLayer.setPanelSettings (&panelSettings);
    systemLayer.setPanelSettings (&panelSettings);

    // Block K (Persistenz gebündelt): erst die Skalar-Zustände der Blöcke
    // A/B/D anwenden, dann die strukturierte Session laden (DIY-Controls,
    // Akkorde, MIDI-In-/Macro-Bindings, MPE-Achsen-Kurven) — VOR dem Bau
    // der Dock-Tabs, damit MpeShapingView/GridSettingsView die geladenen
    // Werte anzeigen (Kurven-Schatten, Feld-Initialwerte).
    keyboard.setPressureSensitivity (panelSettings.getPressureSensitivity());
    keyboard.setSlideSensitivity (panelSettings.getSlideSensitivity());
    keyboard.setPitchBendMultiplier (std::exp2 ((float) (panelSettings.getBendRangeIndex() - 2)));
    keyboard.setInTuneLocation (panelSettings.isInTuneLocationPad()
                                    ? grid::InTuneLocation::pad
                                    : grid::InTuneLocation::finger);
    keyboard.setInTuneWidthPercent ((float) panelSettings.getInTuneWidthPercent());
    mpeMidiSink.setExpressionMode (
        static_cast<grid::ExpressionMode> (panelSettings.getExpressionModeIndex()));

    for (int i = 0; i < std::abs (panelSettings.getOctaveShift()); ++i)
    {
        if (panelSettings.getOctaveShift() > 0)
            keyboard.octaveUp();
        else
            keyboard.octaveDown();
    }

    sessionFile = panelSettings.sessionFile();
    loadSession();
    startTimer (30 * 1000);   // Auto-Save; zusätzlich Save im Destruktor

    // Block L2: Faktor-Geräte + optionale User-Datei (gleiche Ablage wie
    // GridSession.xml). E1b-Schalter (MIDI-Tab): Klartext-Schnellpfad
    // abschaltbar — dann liefert nur die CSV-Profile-Library Geräte.
    if (midiRigSettings.isLegacyCcListEnabled())
        hardwareCcDatabase.load (sessionFile.getSiblingFile ("HardwareDevices.txt"));

    // Track-Tabs (Block H3): Tap = Fokus-Wechsel, gleicher Command-Weg wie
    // der Long-Press-Selector (EngineEditor ruft ebenfalls sendFocusCommand).
    trackTabs.onTrackChosen = [this] (const juce::String& trackKey)
    { sendFocusCommand (trackKey); };

    // Master-Quick-Switch (Block H3): Commit persistiert und re-routet die
    // verwalteten Tracks sofort (Fokus-Command mit neuem Master).
    masterSwitch.onMasterChosen = [this] (const juce::String& name)
    {
        panelSettings.setMasterMidiInputName (name);

        const auto focusKey = TrackFocusBadge::focusRowFrom (liveSetModel).key;
        if (focusKey.isNotEmpty())
            sendFocusCommand (focusKey);
    };

    // Arm-Button (Block H v2): armt/entwaffnet den Conduit-Fokus-Track --
    // dessen Input ist der Grid-MIDI-Out, Record nimmt also Grid-Noten auf.
    // LED folgt der mixer-Domain (refreshTrackFocus); ohne Fokus disabled.
    armButton.onClick = [this]
    {
        const auto focusKey = TrackFocusBadge::focusRowFrom (liveSetModel).key;
        if (focusKey.isEmpty())
            return;

        auto mixerItem = liveSetModel.findItem ("mixer", focusKey);
        const auto armed = mixerItem.isValid()
                           && static_cast<bool> (mixerItem.getProperty ("arm", false));

        touchLiveClient.sendCommand (juce::OSCMessage ("/live/track/set/arm",
                                                       focusKey,
                                                       (juce::int32) (armed ? 0 : 1)));
    };

    releaseAllButton.onClick = [this]
    {
        engine.allNotesOff();
        keyboard.clearLatched();   // latched Akkord mit beenden (dessen
                                   // noteOffs verpuffen nach allNotesOff)
        keyboard.clearDrones();    // Block M: Drones ebenfalls (idempotent)
    };

    // Oktav-Shift (Block D2): über dem Pitch-Ribbon, geklemmt in
    // GridKeyboardComponent (+-kMaxOctaveShift).
    octaveUpTile.onClick   = [this] { keyboard.octaveUp(); panelSettings.setOctaveShift (keyboard.octaveShift()); };
    octaveDownTile.onClick = [this] { keyboard.octaveDown(); panelSettings.setOctaveShift (keyboard.octaveShift()); };

    // Akkord-Speicher (Grid-Page v2, Feature 6): Strip ↔ Keyboard/Memory.
    chordStrip.getConstellation = [this] { return keyboard.constellationNormalized(); };
    chordStrip.onRecall = [this] (int slot) { keyboard.latchConstellation (chordMemory.slot (slot)); };
    chordStrip.onMoveBy = [this] (float dx, float dy) { keyboard.moveLatchedBy (dx, dy); };
    chordStrip.isCcMode = [this] { return ccLayer.isCcMode(); };

    // Session-Skala (Schema 6.2): GridPage liest weiterhin für die
    // Keyboard-Einfärbung mit -- die Anzeige-Kacheln selbst leben seit
    // Block D1 im Settings-Tab (GridSettingsView).
    rootState.addListener (this);
    refreshScaleFromState();   // Initialwert beim Konstruieren lesen

    // Bipolar: Mitte (value01 == 0.5) -> Offset 0, oben -> +1, unten -> -1.
    atOffsetRibbon.onValueChanged = [this] (float value) { engine.setPressureOffset ((value - 0.5f) * 2.0f); };
    slideOffsetRibbon.onValueChanged = [this] (float value) { engine.setSlideOffset ((value - 0.5f) * 2.0f); };

    // Bipolar: Mitte -> 0 HT, oben/unten -> ±kPitchBendOffsetSemitones.
    pitchOffsetRibbon.onValueChanged = [this] (float value)
    {
        engine.setPitchBendOffset ((value - 0.5f) * 2.0f * kPitchBendOffsetSemitones);
    };

    // Modwheel (Block D1, unipolar): sendet CC1 auf dem MPE-Master-Kanal
    // direkt über den MidiDeviceTarget -- kein eigener Sink-Pfad, da nur
    // ein globaler Controller-Wert (kein Voice-Bezug) gebraucht wird.
    modwheelRibbon.onValueChanged = [this] (float value01)
    {
        const auto v = juce::jlimit (0, 127, (int) juce::roundToInt (value01 * 127.0f));
        midiTarget.send (juce::MidiMessage::controllerEvent (
            grid::MpeEncoder::Config{}.memberChannelBase - 1, 1, v));
    };
    modwheelRibbon.setVisible (panelSettings.isModwheelEnabled());

    // Achsen-Farben (Grid-Page v2): user-konfigurierbar und persistent in
    // GridPanelSettings — Initialwerte von dort statt hart kodiert.
    using Axis = grid::GridVoiceEngine::Axis;
    pitchOffsetRibbon.setFillColour (panelSettings.getAxisColour (Axis::PitchBend));
    atOffsetRibbon.setFillColour    (panelSettings.getAxisColour (Axis::Pressure));
    slideOffsetRibbon.setFillColour (panelSettings.getAxisColour (Axis::Slide));

    // Editor-Dock-Panel: ein Tab „MPE" mit dem MPE-Shaping-Editor (S2c),
    // Breite/Offen-Zustand aus der Persistenz laden, Live-Resize + Commit
    // verdrahten. Farbwahl in der MpeShapingView (Quick-Swatch/Picker)
    // aktualisiert die Ribbon-Füllfarben live (Persistenz macht die View).
    auto mpeView = std::make_unique<MpeShapingView> (engine, panelSettings);
    mpeView->onAxisColourChanged = [this] (Axis axis, juce::Colour colour)
    {
        switch (axis)
        {
            case Axis::Pressure:  atOffsetRibbon.setFillColour (colour); break;
            case Axis::Slide:     slideOffsetRibbon.setFillColour (colour); break;
            case Axis::PitchBend: pitchOffsetRibbon.setFillColour (colour); break;
        }
    };
    // Sensitivity-/Range-Regler (Block A2/A3): die View meldet nur den Wert,
    // GridPage reicht ihn an das GridKeyboardComponent durch (Laufzeit-only,
    // keine Persistenz -- Block K).
    mpeView->onSensitivityChanged = [this] (Axis axis, double sensitivity)
    {
        if (axis == Axis::Pressure)
        {
            keyboard.setPressureSensitivity (sensitivity);
            panelSettings.setPressureSensitivity (sensitivity);   // Block K
        }
        else if (axis == Axis::Slide)
        {
            keyboard.setSlideSensitivity (sensitivity);
            panelSettings.setSlideSensitivity (sensitivity);      // Block K
        }
    };
    mpeView->onPitchBendMultiplierChanged = [this] (float multiplier)
    {
        keyboard.setPitchBendMultiplier (multiplier);
        // Block K: Index = log2(¼…×8) + 2 (kMultipliers sind Zweierpotenzen).
        panelSettings.setBendRangeIndex (
            juce::jlimit (0, 5, juce::roundToInt (std::log2 (multiplier)) + 2));
    };
    // M5b: alle GridPage-Tabs sind nur auf der Grid-Page sichtbar
    // (Page-Maske); der Map-Tab (Mappings-Liste) kommt in M5b-Teil 2.
    const auto gridOnly = 1 << TransportBar::pageGrid;
    dockPanel.addTab ("mpe", "MPE", std::move (mpeView), gridOnly);

    // Tab 2 „DIY" (Block F, frueher „CC"): frei baubarer Zusatz-Controller.
    // Die Tab-ID bleibt "cc" (updateCcMode + Persistenz haengen daran),
    // nur der sichtbare Titel heisst DIY. Werkzeugwahl geht ans Overlay;
    // der Edit-Modus gilt bei offenem Panel + aktivem DIY-Tab (updateCcMode).
    auto ccPanel = std::make_unique<CcPanel>();
    ccPanel->onToolChanged = [this] (grid::CcTool tool) { ccLayer.setActiveTool (tool); };
    dockPanel.addTab ("cc", "DIY", std::move (ccPanel), gridOnly);

    // Tab 4 „Macro" (Block E): Ziel-Listen der Controls. Der rohe Zeiger
    // bleibt gueltig, solange das dockPanel (GridPage-Member) lebt.
    auto macroView = std::make_unique<MacroPanel> (macroBindings, midiTarget,
                                                   liveSetModel, touchLiveClient, midiInBindings,
                                                   hardwareCcDatabase, midiProfileLibrary,
                                                   rootState, paramModSink, *this);
    macroPanel = macroView.get();

    // M5c: Grid-Control-Eintraege fuer den Conduit-Picker (live enumeriert
    // beim Oeffnen -- beide Layer, XY mit beiden Achsen).
    macroView->gridControlEntries = [this]
    {
        std::vector<std::pair<grid::MacroControlKey, juce::String>> entries;

        for (const auto layer : { (int) grid::MacroControlKey::system,
                                  (int) grid::MacroControlKey::diy })
        {
            for (const auto& control : modelForLayer (layer).controls())
            {
                const grid::MacroControlKey key { layer, control.id, 0 };
                entries.emplace_back (key, controlDisplayName (key));

                if (control.type == grid::CcTool::xy)
                {
                    const grid::MacroControlKey yKey { layer, control.id, 1 };
                    entries.emplace_back (yKey, controlDisplayName (yKey));
                }
            }
        }

        return entries;
    };
    dockPanel.addTab ("macro", "Macro", std::move (macroView), gridOnly);

    // Tab „Map" (MIDI-Rig M5b): Mappings-Liste + Overlay-Zuweisung — auf
    // ALLEN Pages sichtbar (kAllPages, User-Entscheidung 14.07.2026), das
    // Overlay selbst existiert nur auf der Grid-Page.
    auto mappingsView = std::make_unique<MappingsListComponent> (midiInBindings);
    mappingsView->controlNameFor = [this] (const grid::MacroControlKey& key)
    { return controlDisplayName (key); };
    mappingsView->onLearnRequested = [this] (const grid::MacroControlKey& key)
    { armMapLearn (key); };
    mappingsPanel = mappingsView.get();
    dockPanel.addTab ("map", "Map", std::move (mappingsView));
    mappingsPanel->refresh();   // Namen jetzt auflösbar (controlNameFor gesetzt)

    // Struktur-Änderungen der Bindings: Liste neu aufbauen, Badges der
    // Map-Overlays aktualisieren.
    midiInBindings.onBindingsChanged = [this]
    {
        if (mappingsPanel != nullptr)
            mappingsPanel->refresh();
        ccLayer.repaint();
        systemLayer.repaint();
    };

    // M5b: GridPage besitzt onLearnCompleted (Map-Overlay + Liste hören
    // mit) und leitet an das MacroPanel weiter.
    midiInBindings.onLearnCompleted = [this] (const grid::MacroControlKey& key, int channel,
                                              int number, bool /*isNote*/,
                                              const grid::ModifierSet&)
    {
        if (macroPanel != nullptr)
            macroPanel->handleLearnCompleted (key, channel, number);
        clearMapArmed();
    };

    // Map-Overlay (Ableton-Analogie): Tap armt Learn für das Control
    // (Achse 0); Badges zeigen die gebundene Adresse.
    ccLayer.onMapTapControl = [this] (int controlId)
    { armMapLearn ({ grid::MacroControlKey::diy, controlId, 0 }); };
    systemLayer.onMapTapControl = [this] (int controlId)
    { armMapLearn ({ grid::MacroControlKey::system, controlId, 0 }); };
    ccLayer.mapBadgeTextFor = [this] (int controlId)
    { return mapBadgeFor (grid::MacroControlKey::diy, controlId); };
    systemLayer.mapBadgeTextFor = [this] (int controlId)
    { return mapBadgeFor (grid::MacroControlKey::system, controlId); };

    // M5c: Zweit-Marker am Effektivwert (Modulation eines Grid-Controls).
    const auto modulationProviderFor = [this] (int layer)
    {
        return [this, layer] (int controlId, int axis) -> std::optional<float>
        {
            const grid::MacroControlKey key { layer, controlId, axis };
            if (controlModOffsets.find (key) == controlModOffsets.end())
                return std::nullopt;

            return modulatedControlValue (key, controlValueFor (key));
        };
    };
    ccLayer.modulationValueFor     = modulationProviderFor (grid::MacroControlKey::diy);
    systemLayer.modulationValueFor = modulationProviderFor (grid::MacroControlKey::system);

    // Macro-Wertfluss + Long-Press (Block E), beide Layer: System-Controls
    // des XY+Fader-Modus (layer 0) und DIY-CC-Baukasten (layer 1). Die
    // Layer-Callbacks feuern NUR bei lokalem Touch -- der loest zusaetzlich
    // den Soft-Takeover der MIDI-Eingangs-Bindung (Block G, der externe
    // Fader muss danach neu aufnehmen).
    systemLayer.onControlValueChanged = [this] (const grid::CcControl& control)
    {
        midiInBindings.notifyLocalTouch ({ grid::MacroControlKey::system, control.id, 0 });
        midiInBindings.notifyLocalTouch ({ grid::MacroControlKey::system, control.id, 1 });
        feedMacros (grid::MacroControlKey::system, control);
    };
    ccLayer.onControlValueChanged = [this] (const grid::CcControl& control)
    {
        midiInBindings.notifyLocalTouch ({ grid::MacroControlKey::diy, control.id, 0 });
        midiInBindings.notifyLocalTouch ({ grid::MacroControlKey::diy, control.id, 1 });
        feedMacros (grid::MacroControlKey::diy, control);
    };

    // MIDI-Eingang (Block G) + Noten-Echo (Block H4) als Hub-Abos (ADR 006
    // M1b): Controller-Events kommen vom Controller-Rollen-Geraet, Noten
    // vom Grid-Ausgangs-Geraet (dessen In-Port = Echo-Rueckweg). Der Hub-
    // Tick (~60 Hz, auch ohne Events) treibt Glaettung + Soft-Takeover.
    refreshRigSubscriptions();
    midiRigSettings.addChangeListener (this);   // Rollen-/Geraete-Wechsel → neu binden

    // MIDI-Rig M4 (ADR 006 E2): LED-/Motorfader-Feedback -- feuert, wenn
    // MidiInBindings::tick() einen Wert per Soft-Takeover angewendet hat.
    // Loest die Controller-Rolle + ihr Profil LIVE auf (Muster
    // gridControllerOutputTarget() -- ueberlebt Rollen-/Profil-Wechsel,
    // daher hier EINMAL gesetzt statt in refreshRigSubscriptions()).
    midiInBindings.onFeedbackEcho = [this] (int /*channel*/, int number, bool isNote, float value01)
    {
        const auto context = controllerFeedbackContext();
        if (! context.has_value())
            return;

        // Kanal-agnostisch (M4b): Matching nur ueber Kind + Nummer, gesendet
        // wird auf dem GERAETE-Kanal (RigDevice::midiChannel) -- nicht auf
        // den Kanal-Spalten des CSV (das K1 ist frei umkanalisierbar).
        // M8: Pitch-Bend-Bindungen (Nummer >= 128+1) matchen ueber ihren
        // KANAL (findBySendAddress-Konvention, AlphaTrack-Fader/-Strip).
        const auto isPitchBend = ! isNote && number >= grid::kPitchBendBindingBase;
        const auto* control = context->profile->findBySendAddress (
            isPitchBend ? midirig::AddressKind::pitchBend
                        : (isNote ? midirig::AddressKind::note : midirig::AddressKind::cc),
            isPitchBend ? number - grid::kPitchBendBindingBase : number);
        if (control == nullptr)
            return;

        auto& target = midiPortHub.gridControllerOutputTarget();
        const auto deviceChannel = context->device.midiChannel;
        const auto value7bit = (juce::uint8) juce::jlimit (0, 127, juce::roundToInt (value01 * 127.0f));

        for (const auto& feedback : control->feedback)
        {
            if (feedback.meaning == "display")
                continue;   // Sende-Weg fehlt noch (M8, SysEx-Snippets) -- bewusst stumm

            // M6: Pickup-/Status-Adressen gehoeren EXKLUSIV dem Router --
            // normale Wert-Echos wuerden das Blink-/Aggregat-Signal stoeren.
            if (feedback.meaning.equalsIgnoreCase (midirig::PickupLedRouter::kMeaningLedPickup)
                || feedback.meaning.startsWithIgnoreCase ("status_"))
                continue;

            // M8: position-/Pitch-Bend-Feedback bedient EXKLUSIV der
            // PositionFeedbackRouter (wert-getrieben, touch-gated) -- ein
            // Event-Echo wuerde gegen das Touch-Gate senden.
            if (feedback.meaning.equalsIgnoreCase (midirig::PositionFeedbackRouter::kMeaningPosition)
                || feedback.kind == midirig::AddressKind::pitchBend)
                continue;

            const auto isNoteFb = feedback.kind == midirig::AddressKind::note;

            // Echo-Cache immer aktuell halten (Restore-Basis des Routers) ...
            lastFeedbackSent[{ isNoteFb, feedback.number }] = (int) value7bit;

            // ... aber NICHT senden, wenn der Router die LED gerade selbst
            // bespielt (Shift-Richtung/Ebene, M6.1) -- sonst zieht das Echo die
            // Farbe weg und der Router-Dedup korrigiert nie (Symptom: rot/orange
            // flackern kurz und werden dann dunkel).
            if (pickupLedRouter.isManaging (isNoteFb, feedback.number))
                continue;

            target.send (isNoteFb
                             ? juce::MidiMessage::noteOn (deviceChannel, feedback.number, value7bit)
                             : juce::MidiMessage::controllerEvent (deviceChannel, feedback.number,
                                                                   (int) value7bit));
        }
    };

    // M6: Pickup-LED-Router -- uebersetzt Warte-Zustaende profilgetrieben in
    // Status-/Blink-LEDs (Spalten-Status, Detail-Modus, Shift-Pad-Anzeige).
    // Seams einmalig; Rolle/Profil setzt refreshRigSubscriptions().
    pickupLedRouter.send = [this] (bool isNote, int number, int value7bit)
    {
        const auto context = controllerFeedbackContext();
        if (! context.has_value())
            return;

        const auto channel = context->device.midiChannel;
        midiPortHub.gridControllerOutputTarget().send (
            isNote ? juce::MidiMessage::noteOn (channel, number,
                                                (juce::uint8) juce::jlimit (0, 127, value7bit))
                   : juce::MidiMessage::controllerEvent (channel, number,
                                                         juce::jlimit (0, 127, value7bit)));
    };

    pickupLedRouter.lastEchoValueFor = [this] (bool isNote, int number)
    {
        const auto it = lastFeedbackSent.find ({ isNote, number });
        return it != lastFeedbackSent.end() ? it->second : -1;
    };

    pickupLedRouter.isAddressBound = [this] (bool isNote, int number)
    {
        for (const auto& binding : midiInBindings.all())
            if (binding.isNote == isNote && binding.cc == number)
                return true;

        return false;
    };

    midiInBindings.onPickupStateChanged = [this] (const grid::InputAddress& address,
                                                  const grid::MidiInBindings::PickupState& state)
    { pickupLedRouter.updatePickupState (address, state); };

    // M8: Positions-Feedback-Seams (Motorfader). send baut die MidiMessage:
    // Pitch Bend faehrt auf dem KANAL der Feedback-Adresse (der PB-Kanal IST
    // die Adresse, AlphaTrack-Fader = ch1) -- nicht auf dem Geraete-Kanal.
    positionRouter.send = [this] (const midirig::FeedbackAddress& address, int value14)
    {
        const auto context = controllerFeedbackContext();
        if (! context.has_value())
            return;

        auto& target = midiPortHub.gridControllerOutputTarget();
        const auto clamped = juce::jlimit (0, 16383, value14);

        switch (address.kind)
        {
            case midirig::AddressKind::pitchBend:
                target.send (juce::MidiMessage::pitchWheel (address.channel, clamped));
                break;

            case midirig::AddressKind::cc:
                target.send (juce::MidiMessage::controllerEvent (
                    context->device.midiChannel, address.number, clamped >> 7));
                break;

            case midirig::AddressKind::note:
                target.send (juce::MidiMessage::noteOn (
                    context->device.midiChannel, address.number, (juce::uint8) (clamped >> 7)));
                break;
        }
    };

    // Basiswert der AKTIVEN Ebene (User-Entscheidungen 16.07.2026: Motor
    // zeigt den Basiswert, nicht den M5c-Effektivwert; Ebenen-/Shift-
    // Wechsel laesst ihn auf den Wert der neuen Bank fahren).
    positionRouter.currentBoundValueFor = [this] (int number, bool isNote) -> float
    {
        const auto* binding = midiInBindings.activeBindingForAddress (number, isNote);
        return binding != nullptr ? controlValueFor (binding->key) : -1.0f;
    };

    // M7: Spalten-Resolver -- ordnet eine Eingangs-Adresse ihrer Channelstrip-
    // Spalte zu (Profil-group), damit Live-/Learn-Bindungen die aktive Ebene
    // taggen. Geebent sind nur "normale" Controls einer Spalte: nicht der
    // Ebenen-Selektor (role) und nicht das Status-Push-Control (status_-Feedback).
    midiInBindings.columnResolver = [this] (int, int number, bool isNote) -> juce::String
    {
        const auto context = controllerFeedbackContext();
        if (! context.has_value())
            return {};

        const auto kind = isNote ? midirig::AddressKind::note : midirig::AddressKind::cc;
        const auto* control = context->profile->findBySendAddress (kind, number);
        if (control == nullptr || control->group.isEmpty()
            || control->role.equalsIgnoreCase (midirig::kRoleLayerSelect))
            return {};

        for (const auto& fb : control->feedback)
            if (fb.meaning.startsWithIgnoreCase ("status_"))
                return {};   // Status-Push bleibt ungeebent

        return control->group;
    };

    tickSubToken = midiPortHub.subscribeTick ([this]
    {
        midiInBindings.tick ([this] (const grid::MacroControlKey& key) { return controlValueFor (key); },
                             [this] (const grid::MacroControlKey& key, float value01)
                             { applyExternalValue (key, value01); });

        // M7b: Beat-Position fuer die tempo-synchronen Ebenen-Blinks (8tel
        // aktiv / 16tel Wechsel) VOR dem Router-Tick setzen.
        pickupLedRouter.setBeatPosition (linkClock.getBeatPosition());

        // M6: NACH midiInBindings.tick() -- der Router sieht die Warte-
        // Transitionen desselben Ticks (kein Flackern beim Uebergang).
        pickupLedRouter.tick();

        // M8: Motorfader nachfuehren -- ebenfalls NACH midiInBindings.tick(),
        // damit gerade angewendete Werte im selben Tick gedifft werden.
        positionRouter.tick();
    });

    systemLayer.onLongPressControl = [this] (int controlId)
    { openMacroViewFor (grid::MacroControlKey::system, controlId, systemCcModel); };
    ccLayer.onLongPressControl = [this] (int controlId)
    { openMacroViewFor (grid::MacroControlKey::diy, controlId, ccModel); };

    // Tab 3 „Settings" (Block D1): In-Tune Location/Width + Expression Mode
    // (Block B1/B2/B4), Layout-Feinabstimmung (Edit-Grid-Ersatz), Modwheel-
    // Toggle, Performance-Slide-Out (MIDI-Port + Skala, ehemals Top-Row).
    auto settingsView = std::make_unique<GridSettingsView> (
        rootState, midiPortHub, midiRigSettings, panelSettings,
        keyboard.getInTuneLocation(), (float) panelSettings.getInTuneWidthPercent(),
        mpeMidiSink.expressionMode());
    settingsPanel = settingsView.get();   // Master-Input-Optionen (Block H v2)
    settingsView->onInTuneLocationChanged = [this] (grid::InTuneLocation location)
    {
        keyboard.setInTuneLocation (location);
        panelSettings.setInTuneLocationPad (location == grid::InTuneLocation::pad);   // Block K
    };
    settingsView->onInTuneWidthChanged = [this] (float percent)
    {
        keyboard.setInTuneWidthPercent (percent);
        panelSettings.setInTuneWidthPercent (percent);   // Block K
    };
    settingsView->onExpressionModeChanged = [this] (grid::ExpressionMode mode)
    {
        mpeMidiSink.setExpressionMode (mode);
        panelSettings.setExpressionModeIndex ((int) mode);   // Block K
    };
    settingsView->onLayoutSettingsChanged = [this] { applyRibbonWidth(); };
    settingsView->onModwheelToggled = [this] (bool enabled) { modwheelRibbon.setVisible (enabled); resized(); };
    settingsView->onMasterFavouritesChanged = [this] { refreshMasterSwitch(); };
    settingsView->onTrackTabsChanged = [this] { resized(); };
    settingsView->onRootColourToggled = [this] { refreshTrackFocus(); };
    dockPanel.addTab ("settings", "Settings", std::move (settingsView), gridOnly);

    // M5b: Breite/Offen-Init aus der Persistenz bleibt hier (GridPage hält
    // die GridPanelSettings); die Layout-/Persistenz-Callbacks des Docks
    // (onWidthChanged/onWidthCommitted/onActiveTabChanged) verdrahtet der
    // EngineEditor als Besitzer -- er leitet Tab-Wechsel an
    // refreshDockModes() weiter.
    dockPanel.setPanelWidth (panelSettings.getEditorPanelWidth());
    dockPanel.setPanelOpen (panelSettings.isEditorPanelOpen());

    // Block H v2: Badge/Arm/Master-Optionen folgen dem LiveSetModel --
    // Listener am MEMBER-Handle (Instanz-Falle: ein Temporary wäre No-op).
    liveSetState = liveSetModel.getState();
    liveSetState.addListener (this);
    refreshTrackFocus();
    refreshMasterSwitch();

    updateCcMode();   // Initialzustand (Panel-Open aus panelSettings, aktiver Tab "mpe")
    applyLayoutMode();   // Initialer Layout-Modus aus der Persistenz
}

GridPage::~GridPage()
{
    saveSession();   // Block K: letzter Stand (zusätzlich zum 30-s-Auto-Save)

    // M5b: eigene Dock-Tabs abräumen -- deren Contents referenzieren
    // GridPage-Members und dürfen GridPage nicht überleben (das Dock
    // gehört dem EngineEditor; removeTab feuert keine Callbacks).
    dockPanel.removeTab ("mpe");
    dockPanel.removeTab ("cc");
    dockPanel.removeTab ("macro");
    dockPanel.removeTab ("map");
    dockPanel.removeTab ("settings");

    midiRigSettings.removeChangeListener (this);
    midiPortHub.unsubscribe (controllerSubToken);
    midiPortHub.unsubscribe (controllerNoteSubToken);
    midiPortHub.unsubscribe (noteSubToken);
    midiPortHub.unsubscribe (tickSubToken);
    liveSetState.removeListener (this);
    rootState.removeListener (this);
}

void GridPage::refreshRigSubscriptions()
{
    midiPortHub.unsubscribe (controllerSubToken);
    midiPortHub.unsubscribe (controllerNoteSubToken);
    midiPortHub.unsubscribe (noteSubToken);

    // Controller-Rolle (Block G): CCs fuettern die Bindings (Soft-Takeover).
    controllerSubToken = midiPortHub.subscribeController (
        midiRigSettings.getGridControllerDeviceId(),
        [this] (const midi::ControllerEvent& event)
        {
            // M8: Pitch Bend (Motorfader/Ribbon) -- eigener Bindungs-Pfad
            // (Nummer = 128 + Kanal, Learn inklusive).
            if (event.kind == midi::ControllerEvent::Kind::pitchBend)
            {
                midiInBindings.handleIncomingPitchBend (event.channel, event.value);
                return;
            }

            if (event.kind != midi::ControllerEvent::Kind::cc)
                return;

            // M7: Ebenen-Selektor-Encoder fangen das CC ab (keine Bindung).
            if (routeLayerSelectCc (event.channel, event.number, event.value))
                return;

            midiInBindings.handleIncomingCc (event.channel, event.number, event.value);
        });

    // Controller-Rolle, Noten (M4): Pads senden Noten -- gleicher
    // Bindungs-Pfad wie CCs (Momentary + Velocity, Learn inklusive).
    // M6: der Router beobachtet die Noten zusaetzlich passiv (Status-Push
    // haelt den momentary Detail-Modus) -- die Note bleibt mapp-/lernbar.
    controllerNoteSubToken = midiPortHub.subscribeNotes (
        midiRigSettings.getGridControllerDeviceId(),
        [this] (const midi::NoteEvent& event)
        {
            // M8: Touch-Noten (Fader-/Strip-/Encoder-Beruehrung) sind KEINE
            // Bindungs-Quellen -- der Griff zum Fader wuerde sonst im Learn
            // die Touch-Note binden. Sie gaten nur das Motor-Feedback.
            if (positionRouter.isTouchNote (event.note))
            {
                positionRouter.handleControllerNote (event.note, event.isOn);
                return;
            }

            midiInBindings.handleIncomingNote (event.channel, event.note,
                                               event.velocity, event.isOn);
            pickupLedRouter.handleControllerNote (event.note, event.isOn);
        });

    // Grid-Ausgangs-Rolle, In-Port (Block H4): Noten-Echo aufs Pad-Raster.
    noteSubToken = midiPortHub.subscribeNotes (
        midiRigSettings.getGridOutputDeviceId(),
        [this] (const midi::NoteEvent& event)
        {
            if (event.isOn)
                keyboard.echoNoteOn (event.note, (float) event.velocity / 127.0f);
            else
                keyboard.echoNoteOff (event.note);
        });

    // M6: Takeover-Modus + Controller-Profil der Rolle uebernehmen. Der
    // Setter ist idempotent (laeuft bei JEDEM Registry-Broadcast, ein
    // Settings-Save darf keine Engagements zerstoeren); Geraete-WECHSEL
    // verwirft den Router-Zustand (Restores liefen sonst ans neue Geraet).
    const auto controllerIndex = midiRigSettings.indexOfId (midiRigSettings.getGridControllerDeviceId());
    const auto controllerId = controllerIndex >= 0 ? midiRigSettings.getDevice (controllerIndex).id
                                                   : juce::Uuid::null();
    if (controllerId != pickupRouterDeviceId)
    {
        pickupLedRouter.reset();
        positionRouter.reset();   // M8: Dedupe-Stand gilt nur pro Geraet
        pickupRouterDeviceId = controllerId;
    }

    if (controllerIndex >= 0)
    {
        const auto device = midiRigSettings.getDevice (controllerIndex);
        midiInBindings.setPickupEnabled (device.takeoverMode == TakeoverMode::pickup);

        const auto* profile = device.controllerProfileName.isNotEmpty()
                                  ? controllerProfileLibrary.find (device.controllerProfileName)
                                  : nullptr;
        if (profile != nullptr)
        {
            pickupLedRouter.setProfile (*profile);
            positionRouter.setProfile (*profile);
        }
        else
        {
            pickupLedRouter.clearProfile();
            positionRouter.clearProfile();
        }

        rebuildLayerSelectMap (profile);
        rebuildAddressModes (profile);
    }
    else
    {
        midiInBindings.setPickupEnabled (true);
        pickupLedRouter.clearProfile();
        positionRouter.clearProfile();
        rebuildLayerSelectMap (nullptr);
        rebuildAddressModes (nullptr);
    }
}

std::optional<GridPage::ControllerFeedbackContext> GridPage::controllerFeedbackContext() const
{
    const auto controllerIndex = midiRigSettings.indexOfId (midiRigSettings.getGridControllerDeviceId());
    if (controllerIndex < 0)
        return std::nullopt;

    auto device = midiRigSettings.getDevice (controllerIndex);
    if (device.controllerProfileName.isEmpty())
        return std::nullopt;

    const auto* profile = controllerProfileLibrary.find (device.controllerProfileName);
    if (profile == nullptr)
        return std::nullopt;

    return ControllerFeedbackContext { profile, std::move (device) };
}

void GridPage::rebuildLayerSelectMap (const midirig::ControllerProfile* profile)
{
    layerSelectCcToColumn.clear();
    if (profile == nullptr)
        return;

    for (const auto& control : profile->controls)
    {
        if (! control.role.equalsIgnoreCase (midirig::kRoleLayerSelect)
            || control.group.isEmpty() || control.sendKind != midirig::AddressKind::cc)
            continue;

        layerSelectCcToColumn[control.sendNumber] = { control.group, control.relEncoding };

        // Aktive Ebene der Spalte in Bindungen UND Router-Basis-Anzeige
        // spiegeln (Persistenz-Load hat channelStripLayers ggf. schon gesetzt;
        // beide Setter idempotent -- setColumnLayer startet KEIN Flackern bei
        // der Erst-Initialisierung).
        const auto layer = channelStripLayers.layerFor (control.group);
        midiInBindings.setActiveLayer (control.group, layer);
        pickupLedRouter.setColumnLayer (control.group, layer);
    }
}

void GridPage::rebuildAddressModes (const midirig::ControllerProfile* profile)
{
    // M8: Adress-Modi kanal-agnostisch aus dem Profil (PB-Adressen tragen
    // ihren Kanal in der Nummer). Setter idempotent -- laeuft bei jedem
    // Registry-Broadcast; clearAddressModes verwirft auch die Scrub-Anker.
    midiInBindings.clearAddressModes();
    if (profile == nullptr)
        return;

    for (const auto& control : profile->controls)
    {
        if (control.sendNumber < 0)
            continue;

        const auto number = control.sendKind == midirig::AddressKind::pitchBend
                                ? grid::pitchBendBindingNumber (control.sendChannel)
                                : control.sendNumber;
        const auto isNote = control.sendKind == midirig::AddressKind::note;

        if (control.mode.equalsIgnoreCase (midirig::kModeScrub))
        {
            midiInBindings.setAddressMode (number, isNote, grid::AddressMode::scrub);
            continue;
        }

        if (control.mode.equalsIgnoreCase (midirig::kModeRelative))
        {
            midiInBindings.setAddressMode (number, isNote, grid::AddressMode::relativeTicks,
                                           control.steps, control.relEncoding);
            continue;
        }

        // position-Feedback => Motorfader: Werte greifen sofort (direct),
        // die Adresse wartet nie -- der Motor steht per Definition richtig.
        for (const auto& feedback : control.feedback)
        {
            if (feedback.meaning.equalsIgnoreCase (
                    midirig::PositionFeedbackRouter::kMeaningPosition))
            {
                midiInBindings.setAddressMode (number, isNote, grid::AddressMode::direct);
                break;
            }
        }
    }
}

bool GridPage::routeLayerSelectCc (int, int number, int value7bit)
{
    const auto it = layerSelectCcToColumn.find (number);
    if (it == layerSelectCcToColumn.end())
        return false;

    // Schritt akkumulieren; jeder Schritt frischt den Ebenen-Blink auf, ein
    // Ebenen-Wechsel schaltet die aktive Bank um (naechste Bewegung eines
    // Spalten-Controls loest dann via Soft-Takeover den Pickup aus).
    const auto& column = it->second.column;
    const auto result = channelStripLayers.feed (column, value7bit, it->second.encoding);
    midiInBindings.setActiveLayer (column, result.layer);
    pickupLedRouter.setColumnLayer (column, result.layer);   // dauerhafte Basis + 16tel bei Wechsel
    return true;
}

void GridPage::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &midiRigSettings)
        refreshRigSubscriptions();
}

//==============================================================================
int GridPage::nextScaleRoot (int rootNote) noexcept
{
    return ((rootNote + 1) % 12 + 12) % 12;
}

ScaleType GridPage::nextScaleType (ScaleType type) noexcept
{
    // Block I: Ring über alle 26 Skalen (chromatic + 25 Ableton-Presets) —
    // die Auswahl-UIs bieten zusätzlich das direkte Menü (GridSettingsView).
    return static_cast<ScaleType> ((scale::clampedIndex (static_cast<int> (type)) + 1)
                                   % scale::numScaleTypes);
}

juce::String GridPage::noteNameFor (int rootNote)
{
    static const char* const noteNames[] = { "C", "C#", "D", "D#", "E", "F",
                                             "F#", "G", "G#", "A", "A#", "B" };
    return noteNames[juce::jlimit (0, 11, rootNote)];
}

juce::String GridPage::scaleDisplayNameFor (ScaleType type)
{
    return scaleDisplayName (type);   // Ableton-Schreibweise (Block I)
}

grid::PadGridLayout::Config GridPage::padLayoutConfig() noexcept
{
    // 64 Pads (8×8, Push-Style, User 10.07.2026): die Config-Defaults bleiben
    // 8×4 — nur die Grid-Page setzt rows explizit. lowestNote 48 unverändert,
    // die neuen Reihen wachsen nach OBEN dazu (+5 HT/Reihe).
    grid::PadGridLayout::Config config;
    config.rows = 8;
    return config;
}

void GridPage::refreshScaleFromState()
{
    const auto rootNote = juce::jlimit (0, 11, (int) rootState.getProperty (id::scaleRoot, 0));
    const auto type = scaleTypeFromString (rootState.getProperty (id::scaleType).toString());

    keyboard.setScale (rootNote, type);
}

void GridPage::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == rootState)
    {
        if (property == id::scaleRoot || property == id::scaleType)
            refreshScaleFromState();
        return;
    }

    // Block K: Struktur-Änderungen des Live-Set-Spiegels (Reconnect-Diff,
    // Umbenennung, Geräte-Umbau) → Live-Macro-Ziele re-resolven (coalesced;
    // die heiße parvals-Zeile triggert bewusst NICHT).
    const auto propertyName = property.toString();
    if (propertyName.startsWith ("chain:") || propertyName.startsWith ("parmeta:")
        || propertyName == "name")
        scheduleLiveTargetResolve();

    // Alles Übrige kommt vom LiveSetModel (Block H v2) -- billiger
    // Refresh (Property-Lookups + Änderungs-Guards in den Zielen).
    refreshTrackFocus();
}

void GridPage::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent != rootState)
    {
        scheduleLiveTargetResolve();   // Block K: neue Items (Reconnect)
        refreshTrackFocus();
    }
}

void GridPage::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent != rootState)
    {
        scheduleLiveTargetResolve();   // Block K: Items weg (Track/Device gelöscht)
        refreshTrackFocus();
    }
}

void GridPage::sendFocusCommand (const juce::String& trackKey)
{
    // Grid-MPE-Port aus den Settings (User-Feldtest 11.07.2026: kann vom
    // Conduit-MIDI-Out-Portnamen abweichen); leer = Portname-Fallback.
    auto gridInput = panelSettings.getGridMidiInputName();
    if (gridInput.isEmpty())
        gridInput = midiPortHub.openOutputNameFor (midiRigSettings.getGridOutputDeviceId());

    touchLiveClient.sendCommand (TrackSelectorPanel::makeMidiInputFocusCommand (
        trackKey, gridInput, panelSettings.getMasterMidiInputName(),
        panelSettings.getMasterMidiFavourites().joinIntoString (";")));
}

void GridPage::refreshMasterSwitch()
{
    masterSwitch.setFavourites (panelSettings.getMasterMidiFavourites());
    masterSwitch.setCurrent (panelSettings.getMasterMidiInputName());
}

void GridPage::refreshTrackFocus()
{
    const auto focus = TrackFocusBadge::focusRowFrom (liveSetModel);
    trackTabs.refresh();

    // Echo-Farbe folgt dem Fokus-Track (Block H4); ohne Fokus neutral.
    if (focus.key.isNotEmpty())
        keyboard.setEchoColour (focus.colour);

    // Block I: Root-Pads optional in der Fokus-Track-Farbe (wie Push).
    keyboard.setRootPadColour (panelSettings.isRootPadTrackColour()
                                   && focus.key.isNotEmpty()
                               ? focus.colour
                               : push::colours::padRoot);

    armButton.setEnabled (focus.key.isNotEmpty());
    auto mixerItem = liveSetModel.findItem ("mixer", focus.key);
    armButton.setActive (mixerItem.isValid()
                         && static_cast<bool> (mixerItem.getProperty ("arm", false)));

    // Master-MIDI-Input-Optionen (Settings-Tab) aus der tracks-Domain --
    // setMasterInputOptions dedupliziert selbst.
    if (settingsPanel != nullptr)
    {
        juce::StringArray options;
        if (const auto* array = liveSetModel.getDomain ("tracks")
                                    .getProperty ("input_options").getArray())
            for (const auto& option : *array)
                options.add (option.toString());

        settingsPanel->setMasterInputOptions (options);
    }
}

//==============================================================================
void GridPage::setDockPanelOpen (bool shouldBeOpen) noexcept
{
    dockPanel.setPanelOpen (shouldBeOpen);   // Relayout via onWidthChanged (EngineEditor)
    panelSettings.setEditorPanelOpen (shouldBeOpen);
    updateCcMode();
}

void GridPage::updateCcMode()
{
    const auto open = dockPanel.isPanelOpen();
    const auto tab  = dockPanel.getActiveTabId();

    ccLayer.setCcMode (open && tab == "cc");

    // Map-Modus (M5b): beide Layer (DIY + System-Controls) zeigen das
    // Zuweisungs-Overlay; beim Verlassen wird ein map-gearmtes Learn
    // entschärft (das MacroPanel-Learn bleibt unberührt).
    const auto mapActive = open && tab == "map";
    ccLayer.setMapMode (mapActive);
    systemLayer.setMapMode (mapActive);

    if (! mapActive && mapLearnArmed)
    {
        midiInBindings.cancelLearn();
        clearMapArmed();
    }
}

void GridPage::armMapLearn (const grid::MacroControlKey& key)
{
    midiInBindings.armLearn (key);
    mapLearnArmed = true;
    mapLearnKey = key;

    ccLayer.setMapArmedControl (key.layer == grid::MacroControlKey::diy ? key.controlId : -1);
    systemLayer.setMapArmedControl (key.layer == grid::MacroControlKey::system ? key.controlId : -1);

    if (mappingsPanel != nullptr)
        mappingsPanel->setArmedKey (true, key);
}

void GridPage::clearMapArmed()
{
    mapLearnArmed = false;
    ccLayer.setMapArmedControl (-1);
    systemLayer.setMapArmedControl (-1);

    if (mappingsPanel != nullptr)
        mappingsPanel->setArmedKey (false, {});
}

GridPanelSettings::GridLayoutMode
GridPage::nextLayoutMode (GridPanelSettings::GridLayoutMode mode) noexcept
{
    return mode == GridPanelSettings::GridLayoutMode::fullPads
               ? GridPanelSettings::GridLayoutMode::xyFaders
               : GridPanelSettings::GridLayoutMode::fullPads;
}

void GridPage::toggleLayoutMode()
{
    setLayoutMode (nextLayoutMode (panelSettings.getGridLayoutMode()));
}

void GridPage::setLayoutMode (GridPanelSettings::GridLayoutMode newMode)
{
    panelSettings.setGridLayoutMode (newMode);
    applyLayoutMode();
}

void GridPage::applyLayoutMode()
{
    const auto xyFaders = panelSettings.getGridLayoutMode()
                              == GridPanelSettings::GridLayoutMode::xyFaders;

    // Block H v2: der Modus wird am Grid-Page-Icon abgelesen (Editor
    // schaltet gridMpe ↔ gridMpeXy) -- die früheren Kacheln sind entfallen.
    if (onLayoutModeChanged != nullptr)
        onLayoutModeChanged (panelSettings.getGridLayoutMode());

    // Sauberer Zustand bei jedem Wechsel: Modell leeren, im XY+Fader-Modus
    // frisch bestücken — der Werte-Reset ist akzeptiert (TODO(design):
    // Persistenz der Control-Werte über den Moduswechsel).
    systemCcModel.clear();

    if (xyFaders)
        grid::buildXyFaderLayout (systemCcModel);

    systemLayer.setVisible (xyFaders);
    systemLayer.repaint();
    resized();
}

void GridPage::applyRibbonWidth()
{
    resized();   // liest panelSettings.getRibbonWidthPx() live -- voll dynamisch
}

//==============================================================================
// Macro-System (Block E)

void GridPage::feedMacros (int layer, const grid::CcControl& control)
{
    // M5c: der Macro-Fluss sieht den EFFEKTIVWERT (Basis + Modulations-
    // Offset) -- der Control-Basiswert selbst bleibt unangetastet.
    const auto feed = [this] (const grid::MacroControlKey& key, float raw)
    { macroBindings.applyValue (key, modulatedControlValue (key, raw)); };

    switch (control.type)
    {
        case grid::CcTool::fader:
            feed ({ layer, control.id, 0 }, control.value);
            break;

        case grid::CcTool::push:
        case grid::CcTool::toggle:
            feed ({ layer, control.id, 0 }, control.on ? 1.0f : 0.0f);
            break;

        case grid::CcTool::xy:
            // control.y ist 0 = oben -- fuer Macro-Semantik invertieren
            // (oben = 1, wie beim Fader).
            feed ({ layer, control.id, 0 }, control.x);
            feed ({ layer, control.id, 1 }, 1.0f - control.y);
            break;

        case grid::CcTool::none:
        default:
            break;
    }
}

float GridPage::modulatedControlValue (const grid::MacroControlKey& key,
                                       float rawValue01) const noexcept
{
    const auto it = controlModOffsets.find (key);
    if (it == controlModOffsets.end())
        return rawValue01;

    return juce::jlimit (0.0f, 1.0f, rawValue01 + it->second);
}

void GridPage::setControlModulation (const grid::MacroControlKey& key, float offsetNorm)
{
    controlModOffsets[key] = juce::jlimit (-1.0f, 1.0f, offsetNorm);
    refeedControl (key);
}

void GridPage::clearControlModulation (const grid::MacroControlKey& key)
{
    if (controlModOffsets.erase (key) == 0)
        return;

    refeedControl (key);   // Ziele fallen auf den Basiswert zurueck
}

void GridPage::refeedControl (const grid::MacroControlKey& key)
{
    // Re-Entranz-Guard: moduliert Control A Control B und B (indirekt)
    // wieder A, bricht der zweite Besuch desselben Keys die Kette --
    // der Offset ist gespeichert, nur das erneute Feeden entfaellt.
    if (! controlModFeedGuard.insert (key).second)
        return;

    if (const auto* control = modelForLayer (key.layer).find (key.controlId))
        feedMacros (key.layer, *control);

    (key.layer == grid::MacroControlKey::system ? systemLayer : ccLayer).repaint();
    controlModFeedGuard.erase (key);
}

grid::CcControlModel& GridPage::modelForLayer (int layer) noexcept
{
    return layer == grid::MacroControlKey::system ? systemCcModel : ccModel;
}

float GridPage::controlValueFor (const grid::MacroControlKey& key) noexcept
{
    const auto* control = modelForLayer (key.layer).find (key.controlId);
    if (control == nullptr)
        return 0.0f;

    switch (control->type)
    {
        case grid::CcTool::fader:  return control->value;
        case grid::CcTool::push:
        case grid::CcTool::toggle: return control->on ? 1.0f : 0.0f;
        case grid::CcTool::xy:     return key.axis == 0 ? control->x : 1.0f - control->y;
        case grid::CcTool::none:
        default:                   return 0.0f;
    }
}

void GridPage::applyExternalValue (const grid::MacroControlKey& key, float value01)
{
    auto* control = modelForLayer (key.layer).find (key.controlId);
    if (control == nullptr)
        return;

    // Flankenerkennung (M4b): Toggles schalten pro Pad-Druck UM (steigende
    // Flanke), statt vom Note-Off sofort wieder ausgezogen zu werden --
    // Push bleibt momentary (Feldtest-Fund 14.07.2026, Xone:K1-Pads).
    const auto isHigh = value01 >= 0.5f;
    auto& wasHigh = externalHigh[key];
    const auto risingEdge = isHigh && ! wasHigh;
    wasHigh = isHigh;

    switch (control->type)
    {
        case grid::CcTool::fader:
            control->value = juce::jlimit (0.0f, 1.0f, value01);
            break;

        case grid::CcTool::push:
            control->on = isHigh;
            break;

        case grid::CcTool::toggle:
            if (! risingEdge)
                return;
            control->on = ! control->on;
            break;

        case grid::CcTool::xy:
            if (key.axis == 0)
                control->x = juce::jlimit (0.0f, 1.0f, value01);
            else
                control->y = juce::jlimit (0.0f, 1.0f, 1.0f - value01);   // y: 0 = oben
            break;

        case grid::CcTool::none:
        default:
            return;
    }

    // Anzeige folgt (Block G) + Macro-Ziele bekommen dieselben weichen Werte.
    (key.layer == grid::MacroControlKey::system ? systemLayer : ccLayer).repaint();
    feedMacros (key.layer, *control);
}

// Anzeigename eines Control-Typs (Macro-Titel, Mappings-Liste, M5b).
static juce::String typeNameForTool (grid::CcTool type)
{
    switch (type)
    {
        case grid::CcTool::fader:  return "Fader";
        case grid::CcTool::push:   return "Push";
        case grid::CcTool::toggle: return "Toggle";
        case grid::CcTool::xy:     return "XY-Pad";
        case grid::CcTool::none:   return "Control";
    }

    return "Control";
}

juce::String GridPage::controlDisplayName (const grid::MacroControlKey& key)
{
    const auto* control = modelForLayer (key.layer).find (key.controlId);
    auto name = (control != nullptr ? typeNameForTool (control->type) : juce::String ("Control"))
                + " " + juce::String (key.controlId);

    if (key.axis == 1)
        name << juce::String::fromUTF8 (" \xc2\xb7 Y");
    if (key.layer == grid::MacroControlKey::system)
        name << juce::String::fromUTF8 (" \xc2\xb7 Sys");

    return name;
}

juce::String GridPage::mapBadgeFor (int layer, int controlId)
{
    const auto shortAddress = [] (const grid::MidiInBindings::Binding& binding)
    {
        auto text = binding.isNote
                        ? juce::MidiMessage::getMidiNoteName (binding.cc, true, true, 4)
                        : "CC" + juce::String (binding.cc);
        if (! binding.modifiers.empty())
            text << "+" << juce::String ((int) binding.modifiers.size());   // Shift-Ebene
        return text;
    };

    juce::StringArray parts;
    for (int axis = 0; axis <= 1; ++axis)
        if (const auto* binding = midiInBindings.bindingFor ({ layer, controlId, axis }))
            parts.add ((axis == 1 ? juce::String ("Y:") : juce::String()) + shortAddress (*binding));

    return parts.joinIntoString (" ");
}

void GridPage::openMacroViewFor (int layer, int controlId, grid::CcControlModel& model)
{
    const auto* control = model.find (controlId);
    if (control == nullptr || macroPanel == nullptr)
        return;

    macroPanel->showControl (layer, controlId,
                             typeNameForTool (control->type) + " " + juce::String (controlId),
                             control->type == grid::CcTool::xy);

    dockPanel.setPanelOpen (true);   // Relayout via onWidthChanged (EngineEditor)
    panelSettings.setEditorPanelOpen (true);
    dockPanel.setActiveTab ("macro");
    updateCcMode();
}

void GridPage::resized()
{
    auto bounds = getLocalBounds();

    // M5b: Das Editor-Dock dockt eine Ebene höher im EngineEditor (wie das
    // Browser-Panel) -- sein removeFromRight steckt bereits in den an
    // GridPage übergebenen bounds, hier ist nichts zu reservieren.

    // Block H3: Track-Tabs über die volle Breite (alle MIDI-Tracks,
    // Push-Optik, Halten = Fokus-Wechsel, Ziehen = Scrollen) — Position
    // oben oder unterhalb der Grid (Settings-Tab, Runde 3).
    auto tabsStrip = panelSettings.isTrackTabsBottom() ? bounds.removeFromBottom (28)
                                                       : bounds.removeFromTop (28);
    trackTabs.setBounds (tabsStrip.reduced (2, 0));

    const auto ribbonWidth = panelSettings.getRibbonWidthPx();   // Block D1, live

    // Block D2 (User-Feedback 11.07.): Pitch-Fader-Stapel — Oktav-Buttons
    // darüber, je EIN Pad hoch und volle Ribbon-Breite (statt nebeneinander
    // in einer schmalen Zeile) — Up oben (höherer Pitch = oben am Bildschirm),
    // Down darunter; Release All darunter (ersetzt die frühere Top-Row).
    auto pitchColumn = bounds.removeFromLeft (ribbonWidth);
    const auto padHeight = juce::roundToInt ((float) pitchColumn.getHeight()
                                                 / (float) padLayoutConfig().rows);
    // Master-Quick-Switch oben in der Spalte (User-Feedback 11.07.2026:
    // „runter" -- der Pitch-Fader darf dafür kürzer werden). Abstände wie
    // die Oktav-Buttons: Vollzellen ohne extra Inset (Runde 3).
    masterSwitch.setBounds (pitchColumn.removeFromTop (padHeight));
    octaveUpTile.setBounds (pitchColumn.removeFromTop (padHeight));
    octaveDownTile.setBounds (pitchColumn.removeFromTop (padHeight));
    // Block H3 (User-Feedback): Arm unten LINKS (unter Pitch), Release All
    // wandert nach unten rechts -- beide in Pad-Höhe.
    armButton.setBounds (pitchColumn.removeFromBottom (padHeight));
    pitchOffsetRibbon.setBounds (pitchColumn);

    // Modwheel (Block D1): eigene Spalte direkt neben Pitch, Breite nur
    // reserviert, wenn aktiviert.
    if (modwheelRibbon.isVisible())
        modwheelRibbon.setBounds (bounds.removeFromLeft (ribbonWidth));

    // Release All (Block H3): unten rechts in Pad-Höhe -- darüber
    // Pressure/Slide je zur Hälfte.
    auto rightColumn = bounds.removeFromRight (ribbonWidth);
    releaseAllButton.setBounds (rightColumn.removeFromBottom (padHeight));
    atOffsetRibbon.setBounds    (rightColumn.removeFromTop (rightColumn.getHeight() / 2));
    slideOffsetRibbon.setBounds (rightColumn);

    // Akkord-Speicher-Strip (Grid-Page v2, Feature 6) zwischen Pad-Raster
    // und rechter Ribbon-Spalte — Mock-Formel: quadratische Slots aus der
    // verbleibenden Höhe (8 Slots, 2 px Gap, 1 px Padding).
    const auto stripW = juce::jmax (40, juce::roundToInt (((float) bounds.getHeight() - 16.0f) / 8.0f) + 2);
    chordStrip.setBounds (bounds.removeFromRight (stripW));

    keyboard.setBounds (bounds);
    ccLayer.setBounds (bounds);   // Overlay exakt über den Keyboard-Bounds

    // System-Controls (XY+Fader-Modus): exakt die oberen systemControlRowsAtStartup
    // Zellreihen der Keyboard-Fläche — IMMER positioniert, die Sichtbarkeit
    // entscheidet. Zeilenzahl fix seit Konstruktion (CcControlLayer::rows
    // ist const, siehe systemControlRowsAtStartup-Kommentar im Header).
    const auto systemHeight = juce::roundToInt ((float) bounds.getHeight()
                                  * (float) systemControlRowsAtStartup
                                  / (float) padLayoutConfig().rows);
    systemLayer.setBounds (bounds.withHeight (systemHeight));

    if (bounds.getHeight() > 0)
        chordStrip.setSurfaceAspect ((float) bounds.getWidth() / (float) bounds.getHeight());
}

//==============================================================================
// Block K: Session-Persistenz + Live-Ziel-Re-Resolve

void GridPage::loadSession()
{
    const grid::GridSessionStore::Refs refs { ccModel, chordMemory, midiInBindings,
                                              macroBindings, engine, channelStripLayers };
    grid::GridSessionStore::apply (grid::GridSessionStore::loadFromFile (sessionFile), refs,
                                   [this] (const juce::ValueTree& state)
                                   { return makeTargetFromState (state); });
    resolveLiveMacroTargets();
    ccLayer.repaint();
    chordStrip.repaint();
}

void GridPage::saveSession()
{
    if (sessionFile == juce::File())
        return;

    const grid::GridSessionStore::Refs refs { ccModel, chordMemory, midiInBindings,
                                              macroBindings, engine, channelStripLayers };
    grid::GridSessionStore::saveToFile (sessionFile, grid::GridSessionStore::capture (refs));
}

void GridPage::timerCallback()
{
    saveSession();
}

void GridPage::setHardwarePresetSources (HardwarePresetLibrary& presetLibrary,
                                         HardwarePresetScanner& presetScanner)
{
    if (macroPanel != nullptr)
        macroPanel->setPresetSources (presetLibrary, midiRigSettings, presetScanner);
}

std::unique_ptr<grid::MacroTarget> GridPage::makeTargetFromState (const juce::ValueTree& state)
{
    if (state.hasType (grid::MidiCcTarget::kStateType))
        return std::make_unique<grid::MidiCcTarget> (
            midiTarget, (int) state.getProperty ("channel", 1),
            (int) state.getProperty ("cc", 74));

    if (state.hasType (grid::MidiNrpnTarget::kStateType))
        return std::make_unique<grid::MidiNrpnTarget> (
            midiTarget, (int) state.getProperty ("channel", 1),
            (int) state.getProperty ("number", 0),
            (int) state.getProperty ("min", 0),
            (int) state.getProperty ("max", 16383),
            state.getProperty ("name").toString());

    if (state.hasType (grid::MidiProgramChangeTarget::kStateType))
        return std::make_unique<grid::MidiProgramChangeTarget> (
            midiTarget, (int) state.getProperty ("channel", 1),
            (int) state.getProperty ("bankMsb", -1),
            (int) state.getProperty ("bankLsb", -1));

    // M9c: Preset-Load-Button (Druckflanke -> Bank-Select + PC).
    if (state.hasType (grid::MidiPresetLoadTarget::kStateType))
        return std::make_unique<grid::MidiPresetLoadTarget> (
            midiTarget, (int) state.getProperty ("channel", 1),
            (int) state.getProperty ("program", 0),
            (int) state.getProperty ("bankMsb", -1),
            (int) state.getProperty ("bankLsb", -1),
            state.getProperty ("name").toString());

    if (state.hasType (grid::AbletonParamTarget::kStateType))
        return std::make_unique<grid::AbletonParamTarget> (
            touchLiveClient, grid::AbletonParamTarget::specFromState (state));

    // M5c: Conduit-Ziele -- nodeUuid/Control-Ids sind selbst persistent,
    // kein Resolver-Lauf noetig (Aufloesung transient bei jedem sendValue).
    if (state.hasType (grid::ConduitParamTarget::kStateType))
        return std::make_unique<grid::ConduitParamTarget> (
            paramModSink, rootState,
            state.getProperty ("nodeUuid").toString(),
            state.getProperty ("paramId").toString(),
            (bool) state.getProperty ("bipolar", false),
            (float) (double) state.getProperty ("amount", 1.0),
            state.getProperty ("name").toString());

    if (state.hasType (grid::GridControlModTarget::kStateType))
        return std::make_unique<grid::GridControlModTarget> (
            *this,
            grid::MacroControlKey { (int) state.getProperty ("layer", 0),
                                    (int) state.getProperty ("controlId", 0),
                                    (int) state.getProperty ("axis", 0) },
            (bool) state.getProperty ("bipolar", false),
            (float) (double) state.getProperty ("amount", 1.0),
            state.getProperty ("name").toString());

    return nullptr;   // unbekannter Typ: Slot bleibt ohne Ziel
}

void GridPage::resolveLiveMacroTargets()
{
    for (const auto& key : macroBindings.allKeys())
    {
        for (int i = 0; i < macroBindings.count (key); ++i)
        {
            auto* binding = macroBindings.get (key, i);
            if (binding == nullptr)
                continue;

            auto* live = dynamic_cast<grid::AbletonParamTarget*> (binding->target.get());
            if (live == nullptr)
                continue;

            const auto resolved = grid::resolveLiveParam (liveSetModel, live->spec());
            if (resolved.found)
                live->resolve (resolved.deviceId, resolved.parameterIndex,
                               resolved.minValue, resolved.maxValue, resolved.quantised);
            else
                live->unresolve();   // Live-Neustart: dvid ist tot, bis der Spiegel steht
        }
    }
}

void GridPage::scheduleLiveTargetResolve()
{
    if (liveResolvePending)
        return;

    liveResolvePending = true;
    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<GridPage> (this)]
    {
        if (safeThis == nullptr)
            return;

        safeThis->liveResolvePending = false;
        safeThis->resolveLiveMacroTargets();
    });
}

} // namespace conduit
