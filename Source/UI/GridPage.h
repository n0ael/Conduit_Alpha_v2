#pragma once

#include <map>
#include <optional>
#include <set>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "CcControlLayer.h"
#include "ChordMemoryStrip.h"
#include "Core/CcControlModel.h"
#include "Core/ChannelStripLayers.h"
#include "Core/ChordMemory.h"
#include "Core/ConduitMacroTargets.h"
#include "Core/GridPanelSettings.h"
#include "Core/GridSessionStore.h"
#include "Core/HardwareCcDatabase.h"
#include "Core/GridVoiceEngine.h"
#include "Core/LinkClock.h"
#include "Core/MacroBindings.h"
#include "Core/ControllerProfileLibrary.h"
#include "Core/MidiInBindings.h"
#include "Core/MidiPortHub.h"
#include "Core/MidiProfileLibrary.h"
#include "Core/MpeMidiSink.h"
#include "Core/PickupLedRouter.h"
#include "Core/PositionFeedbackRouter.h"
#include "EditorDockPanel.h"
#include "ExpressionRibbon.h"
#include "GridKeyboardComponent.h"
#include "MasterDeviceSwitch.h"
#include "PushTiles.h"
#include "TouchLive/LiveSetModel.h"
#include "TouchLive/TouchLiveClient.h"
#include "TrackFocusBadge.h"
#include "TrackTabsStrip.h"
#include "Util/ScaleQuantizer.h"

namespace conduit
{

class GridSettingsView;
class MacroPanel;
class MappingsListComponent;

//==============================================================================
/**
    Grid-Page (Ω, M1 Teil 3 — erster spielbarer Ton; Grid-Page v2 —
    Ribbon-Umbau nach Design-Mock; Block D2 — Performance-Layout-Umbau):
    GridKeyboardComponent (Hauptfläche) flankiert von bipolaren Rand-
    Ribbons (Mitte = neutral) — links „Pitch" (±12 Halbtöne,
    GridVoiceEngine::setPitchBendOffset, grün) mit Oktav-Buttons darüber
    (GridKeyboardComponent::octaveUp/Down) und Release-All darunter
    (GridVoiceEngine::allNotesOff), rechts EINE Spalte mit „Pressure" oben
    (GridVoiceEngine::setPressureOffset, orange) über „Slide" unten
    (GridVoiceEngine::setSlideOffset, cyan), optional ein unipolares
    Modwheel-Ribbon direkt neben Pitch (Block D1, sendet CC1 über den
    MidiDeviceTarget). Ribbon-Breite gemeinsam einstellbar
    (GridPanelSettings::ribbonWidthPx, Settings-Tab). Die frühere MIDI-
    Port-/Skala-Top-Row ist ins Settings-Tab umgezogen (Performance-Slide-
    Out, GridSettingsView) — nur die Layout-Modus-Kacheln bleiben oben
    links. Das frühere Volume-Ribbon ist entfallen; GridVoiceEngine::
    setGlobalVolume bleibt für Tests/Zukunft bestehen.

    Rechtes Editor-Dock-Panel (S2-Vorstufe MPE-Shaping; seit MIDI-Rig M5b
    app-weit im EngineEditor angedockt, Muster BrowserPanel — GridPage
    registriert seine Tabs mit Page-Maske „nur Grid-Page" und räumt sie
    im Dtor ab). Tab „MPE" mit MpeShapingView (S2c) -- drei touch-
    editierbare Kurven (Pressure/Slide/PitchBend) + Live-Noten-Kreise, je
    Achse eine Detailspalte mit Sensitivity-Regler bzw. PitchBend-Range-
    Multiplikator (Block A2/A3), Offset-Schloss und Achsfarbe. Toggle über
    einen eigenen TransportBar-Button (setDockPanelOpen), Breite/Offen-
    Zustand persistiert über GridPanelSettings (App-Zustand, Muster
    MeterSettings).

    CC-Baukasten (Grid-Page v2): zweiter Tab „CC" (CcPanel, Werkzeuge
    Fader/Push/Toggle/XY) + CcControlLayer als Overlay exakt über den
    Keyboard-Bounds (nach keyboard deklariert/hinzugefügt = darüber).
    CC-Modus (Bearbeiten) gilt, wenn das Dock-Panel offen ist UND der
    aktive Tab „cc" — aktualisiert in setDockPanelOpen und über
    EditorDockPanel::onActiveTabChanged (updateCcMode).

    Settings-Tab (Block D1, GridSettingsView): dritter Tab -- Performance-
    Slide-Out (MIDI-Ausgangsport + Session-Skala-Kacheln, ehemals Top-Row),
    In-Tune Location/Width (Block B1/B2), Expression Mode MPE/Poly-AT/
    Mono-AT (Block B4, GridVoiceEngine unbeteiligt -- reicht direkt an
    MpeMidiSink::setExpressionMode), Layout-Feinabstimmung (XY-Zeilen +
    Fader-Breite als Zahlenfelder, Ersatz für die freie Drag-Resize-Fläche
    der Roadmap-Beschreibung, TODO(design)), Modwheel-Toggle. Die View
    bindet selbst an rootState (eigener ValueTree::Listener für die
    Skala-Kacheln) und meldet alles andere über Callbacks, exakt wie
    MpeShapingView/CcPanel.

    Session-Skala (Grid-Page v2, Design-Mock): GridPage selbst hört nur noch
    für die Keyboard-Einfärbung auf den Root-ValueTree (refreshScaleFromState)
    — die Anzeige-Kacheln leben seit Block D1 im Settings-Tab. Geschrieben
    wird NUR in den Root-ValueTree (UI bindet nie an den Processor,
    CLAUDE.md 5.3).

    Pad-Layout-Modi (User 10.07.2026): das Raster ist 8×8 (64 Pads,
    Push-Style, padLayoutConfig()). Zwei IconTiles (gridMpe/gridMpeXy) oben
    links schalten zwischen „64 Pads" und „XY+Fader" um (persistent,
    GridPanelSettings::gridLayoutMode). Im XY+Fader-Modus überdeckt ein
    eigener systemLayer (CcControlLayer über systemCcModel,
    8×systemControlRowsAtStartup-Zellraster, IMMER Play-Modus) die oberen
    Pad-Reihen mit fester Bestückung (grid::buildXyFaderLayout: 1× XY +
    6 Fader) — das 8×8-Noten-Mapping des Keyboards bleibt unverändert, die
    überdeckten Pads sind schlicht unspielbar. Der systemLayer liegt ÜBER
    dem User-ccLayer (nach ihm hinzugefügt) und gewinnt dessen Hit-Tests
    auch im CC-Tab-Modus — dass die System-Controls dort SPIELBAR bleiben,
    ist akzeptiert (TODO(design)).
*/
class GridPage final : public juce::Component,
                       public grid::IGridControlModSink,
                       private juce::ValueTree::Listener,
                       private juce::Timer,
                       private juce::ChangeListener
{
public:
    GridPage (juce::ValueTree rootStateToUse,
              grid::GridVoiceEngine& engineToUse,
              GridPanelSettings& panelSettingsToUse, grid::MpeMidiSink& mpeMidiSinkToUse,
              LiveSetModel& liveSetModelToUse, TouchLiveClient& touchLiveClientToUse,
              MidiPortHub& midiPortHubToUse, MidiRigSettings& midiRigSettingsToUse,
              MidiProfileLibrary& midiProfileLibraryToUse,
              ControllerProfileLibrary& controllerProfileLibraryToUse,
              EditorDockPanel& dockPanelToUse,
              IParamModulationSink& paramModSinkToUse,
              LinkClock& linkClockToUse);
    ~GridPage() override;

    //==========================================================================
    // M5c (IGridControlModSink): Modulation eines Grid-Control-Werts —
    // Basiswert bleibt unangetastet, nur Ausgabe (feedMacros) und Anzeige
    // (zweiter Marker im Layer) verwenden den Effektivwert. Re-Entranz-
    // Guard bricht Zyklen (Control A moduliert B moduliert A).

    void setControlModulation (const grid::MacroControlKey& key, float offsetNorm) override;
    void clearControlModulation (const grid::MacroControlKey& key) override;

    /** M5b: Dock-Tab hat gewechselt (EngineEditor leitet
        onActiveTabChanged weiter) — CC-/Map-Modus der Overlays neu
        bestimmen. */
    void refreshDockModes() { updateCcMode(); }

    void resized() override;

    /** Toggle vom TransportBar-Button (unabhängig vom Browser) -- schaltet
        das rechte Editor-Dock-Panel und persistiert den Zustand. */
    void setDockPanelOpen (bool shouldBeOpen) noexcept;
    [[nodiscard]] bool isDockPanelOpen() const noexcept { return dockPanel.isPanelOpen(); }

    /** Block H: Tap auf den Grid-Page-Button bei schon aktiver Grid-Page
        (EngineEditor) — schaltet 64-Pad ↔ XY+Fader um (persistiert). */
    void toggleLayoutMode();

    /** Toggle-Zyklus des Layout-Modus — pure, testbar ohne Instanz. */
    [[nodiscard]] static GridPanelSettings::GridLayoutMode
        nextLayoutMode (GridPanelSettings::GridLayoutMode mode) noexcept;

    /** Block H v2: Layout-Modus angewendet (auch initial) — der Editor
        stellt das Grid-Page-Icon um (gridMpe ↔ gridMpeXy, die früheren
        Modus-Kacheln oben links sind entfallen). */
    std::function<void (GridPanelSettings::GridLayoutMode)> onLayoutModeChanged;

    /** Block H v2 rev5: Fokus-Command senden (Track-Tabs, Selector,
        Master-Switch-Re-Route) — Grid-MPE-Port aus den Settings (leer =
        Portname des offenen Grid-MIDI-Outs), Master + Favoriten aus den
        Settings (Favoriten reisen mit, damit das Script Tracks auf JEDEM
        Favoriten-Port als verwaltet erkennt — auch nach Live-Neustart). */
    void sendFocusCommand (const juce::String& trackKey);

    //==========================================================================
    // Kachel-Zyklen der Skala-Anzeige (Session-Skala, Design-Mock Grid-Page
    // v2) — pure functions, testbar ohne GridPage-Instanz.

    /** Root-Kachel: C→C#→…→B→C. */
    [[nodiscard]] static int nextScaleRoot (int rootNote) noexcept;

    /** Skala-Kachel: chromatic→major→minor→pentatonic→chromatic. */
    [[nodiscard]] static ScaleType nextScaleType (ScaleType type) noexcept;

    /** Notenname der Root-Kachel ("C" … "B"). */
    [[nodiscard]] static juce::String noteNameFor (int rootNote);

    /** Anzeigename der Skala-Kachel — toString mit großem Anfangsbuchstaben
        ("Chromatic", "Major", "Minor", "Pentatonic"). */
    [[nodiscard]] static juce::String scaleDisplayNameFor (ScaleType type);

    /** Pad-Raster der Grid-Page: 8×8 (64 Pads, Push-Style, User 10.07.2026).
        Die PadGridLayout-Config-Defaults bleiben bewusst 8×4 (andere
        Nutzer/Tests) — nur die Grid-Page setzt rows explizit; lowestNote 48
        unverändert, die neuen Reihen wachsen nach OBEN dazu. */
    [[nodiscard]] static grid::PadGridLayout::Config padLayoutConfig() noexcept;

private:
    /** Liest scaleRoot/scaleType aus dem Root-Tree und aktualisiert die
        Keyboard-Einfärbung — Anzeige folgt IMMER dem ValueTree (5.3). Die
        Skala-Kacheln selbst leben seit Block D1 im Settings-Tab
        (GridSettingsView, eigener Listener dort). */
    void refreshScaleFromState();

    /** CC-Modus des Overlays = (Dock-Panel offen) UND (aktiver Tab "cc");
        Map-Modus (M5b) analog mit Tab "map" — beim Verlassen wird ein
        map-gearmtes Learn entschärft. */
    void updateCcMode();

    //==========================================================================
    // Map-Modus (MIDI-Rig M5b): Tap im Overlay bzw. Learn-Kachel der
    // Mappings-Liste armt MIDI-Learn für ein Control; onLearnCompleted
    // (GridPage-eigen) räumt auf und informiert MacroPanel + Liste.

    void armMapLearn (const grid::MacroControlKey& key);
    void clearMapArmed();

    /** Anzeigename eines Control-Werts ("Fader 3 · Y · Sys") für
        Mappings-Liste und Badges. */
    [[nodiscard]] juce::String controlDisplayName (const grid::MacroControlKey& key);

    /** Badge-Text eines Controls im Map-Overlay (Adresse der Achse 0,
        XY: beide Achsen) — leer = ungebunden. */
    [[nodiscard]] juce::String mapBadgeFor (int layer, int controlId);

    /** Ribbon-Breite (Block D1 "Fader-Breiten", GridPanelSettings-Wert) hat
        sich geändert -- ruft nur resized() auf (voll live, im Gegensatz zur
        systemControlRows-Zeilenzahl, siehe systemControlRowsAtStartup). */
    void applyRibbonWidth();

    /** Modus-Kachel-Tap: persistiert den Layout-Modus und wendet ihn an. */
    void setLayoutMode (GridPanelSettings::GridLayoutMode newMode);

    /** Kachel-Aktivzustand + Sichtbarkeit/Bestückung der System-Controls
        (XY+Fader-Modus) — resized() positioniert den systemLayer immer,
        die Sichtbarkeit entscheidet. */
    void applyLayoutMode();

    void valueTreePropertyChanged (juce::ValueTree& tree,
                                   const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child,
                                int index) override;

    /** MidiRigSettings-Broadcast (Rollen-/Geräte-Änderung) → Abos neu
        binden (der Hub re-synct seine Ports selbst). */
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    /** Block H v2: Tabs (Fokus-Track), Arm-LED, Grid-Rahmen und Master-
        Input-Optionen aus dem LiveSetModel aktualisieren (tracks-/mixer-
        Domain). */
    void refreshTrackFocus();

    /** Block H3: Quick-Switch mit Favoriten + aktuellem Master füttern. */
    void refreshMasterSwitch();

    //==========================================================================
    // Block K (Persistenz gebündelt): strukturierte Grid-Session
    // (GridSessionStore — DIY-Controls, Akkord-Slots, MIDI-In-/Macro-
    // Bindings, MPE-Achsen-Kurven) laden/speichern + Ableton-Ziel-
    // Re-Resolve (LiveTargetResolver — dvid ist Laufzeit-ID).

    void loadSession();
    void saveSession();

    /** Macro-Ziel aus seinem persistierten toState()-Tree bauen
        (MidiCcTarget/AbletonParamTarget — Live-Ziele starten unresolved). */
    [[nodiscard]] std::unique_ptr<grid::MacroTarget>
        makeTargetFromState (const juce::ValueTree& state);

    /** Alle Live-Macro-Ziele gegen den aktuellen Live-Set-Spiegel neu
        auflösen (nach Laden, Live-Reconnect, Track-/Device-Umbenennung). */
    void resolveLiveMacroTargets();
    /** Coalesced-Async-Trigger (devices-Domain-Änderungen feuern in Serien). */
    void scheduleLiveTargetResolve();

    /** Auto-Save der Session (30 s — plus Save im Destruktor). */
    void timerCallback() override;

    // Bereich des PitchBend-Offset-Ribbons: Mitte = 0, ±Ende = ±12 Halbtöne.
    // Spätere 1–96-Range-UI ersetzt diese Konstante.
    static constexpr float kPitchBendOffsetSemitones = 12.0f;

    juce::ValueTree rootState;  // ref-counted Handle (Session-Skala), nie der Processor (5.3)
    grid::GridVoiceEngine& engine;

    // MIDI-Rig (ADR 006 M1b): der Hub besitzt alle Ports; midiTarget ist
    // die Rollen-Fassade "Grid-Ausgang" (löst das Gerät bei jedem send()
    // live aus der Registry auf — übersteht Rollen-Wechsel).
    MidiPortHub& midiPortHub;
    MidiRigSettings& midiRigSettings;
    MidiProfileLibrary& midiProfileLibrary;   // M2: CSV-Profile (Hardware-Picker)
    ControllerProfileLibrary& controllerProfileLibrary;   // M4: LED-/Motorfader-Feedback
    LinkClock& linkClock;   // M7b: Beat-Position fuer tempo-synchrone Ebenen-Blinks
    grid::IMidiOutputTarget& midiTarget;

    GridPanelSettings& panelSettings;
    IParamModulationSink& paramModSink;   // M5c: GraphManager (Conduit-Param-Ziele)
    grid::MpeMidiSink& mpeMidiSink;   // Block D1: Expression-Mode-Umschaltung (Settings-Tab)
    LiveSetModel& liveSetModel;       // Block E: Ableton-Parameter-Browser (Macro-Ziele)
    TouchLiveClient& touchLiveClient;

    // Macro-System (Block E): Bindings-Store der Controls (System + DIY),
    // Laufzeit-only (Persistenz Block K). macroPanel zeigt in den vom
    // dockPanel besessenen Tab-Content — der GridPage-Dtor entfernt die
    // eigenen Tabs (removeTab), der rohe Zeiger überlebt GridPage nie.
    grid::MacroBindings macroBindings;
    MacroPanel* macroPanel = nullptr;

    // Block L2: Hardware-CC-Datenbank (Device -> Parameter fuer den
    // Macro-Panel-Ziel-Typ "Hardware") -- geladen im Ctor (Faktor-Daten +
    // optionale User-Datei neben GridSession.xml).
    grid::HardwareCcDatabase hardwareCcDatabase;

    // Block H v2: Settings-Tab-Content (roher Zeiger wie macroPanel — das
    // dockPanel besitzt ihn, Lebensdauer identisch) für die Master-Input-
    // Optionen; liveSetState = MEMBER-Handle des LiveSetModel-Trees
    // (Listener hängen an der Instanz — Temporary wäre ein No-op).
    GridSettingsView* settingsPanel = nullptr;
    juce::ValueTree liveSetState;

    // M5b: Mappings-Liste des Map-Tabs (roher Zeiger wie macroPanel) +
    // Zustand des map-gearmten Learns (Overlay-Markierung).
    MappingsListComponent* mappingsPanel = nullptr;
    bool mapLearnArmed = false;
    grid::MacroControlKey mapLearnKey;

    // MIDI-Eingang (Block G): externe CCs bewegen Controls -- Soft-Takeover
    // + Glaettung leben in midiInBindings; die Events kommen als Hub-Abos
    // (Controller-Rolle bzw. Grid-Ausgangs-Rolle fuers Noten-Echo, Block
    // H4). refreshRigSubscriptions() bindet bei Rollen-/Registry-Wechseln
    // neu (MidiRigSettings-ChangeBroadcast, async).
    grid::MidiInBindings midiInBindings;
    int controllerSubToken = 0;
    int controllerNoteSubToken = 0;   // M4: Pad-Noten der Controller-Rolle
    int noteSubToken = 0;
    int tickSubToken = 0;

    // M6: Pickup-LED-Router (profilgetriebene Warte-/Status-LEDs) +
    // Echo-Cache pro Feedback-Adresse {isNote, number} -- der Router
    // restauriert daraus beim Verlassen eines Blink-Zustands.
    midirig::PickupLedRouter pickupLedRouter;
    std::map<std::pair<bool, int>, int> lastFeedbackSent;
    juce::Uuid pickupRouterDeviceId = juce::Uuid::null();   // Geraete-Wechsel = reset()

    // M8: wert-getriebenes Positions-Feedback (Motorfader, AlphaTrack) --
    // profil-getrieben (`position`-Feedback + Touch-Gate), pro Tick gedifft.
    midirig::PositionFeedbackRouter positionRouter;

    // M7: Channelstrip-Ebenen -- Top-Encoder (role=layer_select) waehlen pro
    // Spalte eine von 3 Binding-Baenken. Persistiert pro Session (GridSessionStore).
    midirig::ChannelStripLayers channelStripLayers;

    // CC-Nummer -> Spalte + Encoder-Kodierung fuer role=layer_select-Encoder
    // (aus dem aktiven Profil, gepflegt in refreshRigSubscriptions -- kein
    // Profil-Scan je Event). M8: die Kodierung ist geraeteabhaengig und muss
    // auch hier aus dem Profil kommen (nicht nur bei den Bindungen).
    struct LayerSelectEntry
    {
        juce::String column;
        midirig::RelativeEncoding encoding = midirig::RelativeEncoding::twosComplement;
    };
    std::map<int, LayerSelectEntry> layerSelectCcToColumn;

    /** M7: leitet ein Controller-CC zum Ebenen-Selektor, wenn seine Nummer im
        aktiven Profil role=layer_select traegt (true = verbraucht, nicht an die
        Bindungen weiterreichen). */
    bool routeLayerSelectCc (int channel, int number, int value7bit);

    /** M7: den Selektor-Cache aus dem aktiven Profil neu aufbauen (nullptr =
        leeren). Zusaetzlich fuer jede neue Spalte die aktive Ebene mit der
        aus channelStripLayers gemerkten synchronisieren. */
    void rebuildLayerSelectMap (const midirig::ControllerProfile* profile);

    /** M8: Adress-Modi der MidiInBindings aus dem aktiven Profil neu setzen
        (nullptr = alles absolut): mode=scrub/relative aus dem CSV, `direct`
        fuer Controls mit position-Feedback (Motorfader warten nie). */
    void rebuildAddressModes (const midirig::ControllerProfile* profile);

    /** Controller-Rolle + Profil live aufgeloest (Echo/Router/Restore) --
        nullopt, wenn Rolle unbesetzt oder Profil fehlt/nicht geladen. */
    struct ControllerFeedbackContext
    {
        const midirig::ControllerProfile* profile = nullptr;
        RigDevice device;
    };
    [[nodiscard]] std::optional<ControllerFeedbackContext> controllerFeedbackContext() const;

    // M4b: letzter externer High/Low-Zustand pro Control-Key -- Toggles
    // schalten nur auf steigender Flanke um (applyExternalValue).
    std::map<grid::MacroControlKey, bool> externalHigh;

    // M5c: aktive Grid-Control-Modulationen (Offset [-1..+1] pro Key) +
    // Re-Entranz-Guard (Zyklen A→B→A terminieren beim zweiten Besuch).
    std::map<grid::MacroControlKey, float> controlModOffsets;
    std::set<grid::MacroControlKey> controlModFeedGuard;

    /** Effektivwert eines Control-Werts (roh + Modulations-Offset,
        geklemmt) — Identität ohne aktiven Offset. */
    [[nodiscard]] float modulatedControlValue (const grid::MacroControlKey& key,
                                               float rawValue01) const noexcept;

    /** Macro-Fluss des Controls hinter key neu anstoßen (Modulations-
        Änderung) + Layer repainten — guard-geschützt. */
    void refeedControl (const grid::MacroControlKey& key);

    void refreshRigSubscriptions();

    /** Wertfluss Control → Macro-Ziele (beide Layer, Block E). */
    void feedMacros (int layer, const grid::CcControl& control);
    /** Long-Press: Macro-Ansicht des Controls oeffnen. */
    void openMacroViewFor (int layer, int controlId, grid::CcControlModel& model);

    /** Control-Modell eines Macro-Layers (system/diy, Block E/G). */
    [[nodiscard]] grid::CcControlModel& modelForLayer (int layer) noexcept;
    /** Ist-Wert eines Control-WERTS fuer den Soft-Takeover (Block G). */
    [[nodiscard]] float controlValueFor (const grid::MacroControlKey& key) noexcept;
    /** Externen (geglaetteten) Wert anwenden: Control-Feld setzen, Layer
        repainten, Macro-Ziele fuettern (Block G). */
    void applyExternalValue (const grid::MacroControlKey& key, float value01);

    // XY+Fader-Modus: Zeilenzahl des systemLayer -- CcControlLayer::rows ist
    // const (kein Laufzeit-Resize), daher bei GridPage-Konstruktion aus
    // GridPanelSettings gecacht. Ein Wechsel im Settings-Tab persistiert
    // sofort, wirkt aber erst beim naechsten Neuaufbau der Grid-Page
    // (TODO(design): echtes Laufzeit-Resize braucht CcControlLayer-Umbau).
    const int systemControlRowsAtStartup;

    // Block H3: Track-Tabs (alle MIDI-Tracks, Push-Optik, Tap = Fokus) +
    // Master-Quick-Switch oben in der Pitch-Spalte (User-Feedback).
    // Arm-Button für den Fokus-Track (LED aus der mixer-Domain).
    TrackTabsStrip trackTabs;
    MasterDeviceSwitch masterSwitch;
    push::TextTile armButton { "Arm", push::colours::ledRed };
    push::TextTile releaseAllButton { "Release All", push::colours::ledRed };
    push::TextTile octaveUpTile   { "Oct +" };
    push::TextTile octaveDownTile { "Oct -" };
    ExpressionRibbon atOffsetRibbon      { "Pressure", true };  // bipolar
    ExpressionRibbon slideOffsetRibbon   { "Slide", true };     // bipolar
    ExpressionRibbon pitchOffsetRibbon   { "Pitch", true };     // bipolar
    ExpressionRibbon modwheelRibbon      { "Mod", false };      // unipolar, Block D1 (an/aus)
    GridKeyboardComponent keyboard;
    grid::CcControlModel ccModel;     // CC-Baukasten (Grid-Page v2)
    CcControlLayer ccLayer;           // Overlay ÜBER dem Keyboard (nach keyboard deklariert)
    grid::CcControlModel systemCcModel;  // System-Controls des XY+Fader-Modus (User 10.07.2026)
    CcControlLayer systemLayer;          // ÜBER dem ccLayer, IMMER Play-Modus (kein Werkzeug)
    grid::ChordMemory chordMemory;    // Akkord-Speicher (Grid-Page v2, 8 LCD-Slots)

    // Block K: Session-Datei (neben GridPanel.settings) + Resolve-Coalescing.
    juce::File sessionFile;
    bool liveResolvePending = false;
    ChordMemoryStrip chordStrip { chordMemory };   // liegt räumlich NEBEN dem Keyboard/ccLayer

    // M5b: Das Dock gehört dem EngineEditor (app-weit, Muster BrowserPanel)
    // — GridPage registriert nur seine Tabs (Ctor) und räumt sie im Dtor
    // per removeTab ab (die Contents referenzieren GridPage-Members).
    EditorDockPanel& dockPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridPage)
};

} // namespace conduit
