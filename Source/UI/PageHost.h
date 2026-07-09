#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PushIcons.h"

namespace conduit
{

//==============================================================================
/**
    Host der Pages hinter den Push-Icons der TransportBar
    (User-Entscheidung 2026-07-02; TouchLive statt Clip 09.07.2026):

      Grid      (Ω)             — MPE-Touch-Controller (implementiert)
      Mixer     (∥∥)            — Mixer-Page           [eigener Meilenstein]
      TouchLive (Mini-Kanalzüge) — Ableton-Live-Remote (M1c); die Clip-Page
                                  (▷▭, Fugue-Machine-Sequencer) bekommt
                                  später wieder einen eigenen Slot
      Device    (|||)           — die Patch-Canvas (bestehend)
      Looper    (oo)            — Retro-Looper (B3), erreichbar über die
                                  Tape-Kachel statt über ein Page-Icon

    Der Host besitzt nur die Platzhalter; NodeCanvas, LooperPage, GridPage
    und TouchLivePage gehören weiterhin dem EngineEditor und werden als
    Referenz eingehängt — ihre Verdrahtung bleibt unangetastet.

    Page-Indizes == TransportBar::PageIndex. Message Thread.
*/
class PageHost final : public juce::Component
{
public:
    /** devicePage (Canvas), looperPage, gridPage und touchLivePage: nicht
        owned, müssen den Host überleben — Member-Reihenfolge im EngineEditor. */
    PageHost (juce::Component& devicePage, juce::Component& looperPage,
              juce::Component& gridPage, juce::Component& touchLivePage);

    void setPage (int pageIndex);
    [[nodiscard]] int getPage() const noexcept { return currentPage; }

    void resized() override;

private:
    //==========================================================================
    /** Gestylter Platzhalter im Push-Look: großes Icon + Titel + Hinweis. */
    class Placeholder final : public juce::Component
    {
    public:
        Placeholder (push::Icon iconToUse, juce::String titleToUse);
        void paint (juce::Graphics& g) override;

    private:
        push::Icon icon;
        juce::String title;
    };

    juce::Component& device;
    juce::Component& looper;
    juce::Component& grid;
    juce::Component& touchLive;
    Placeholder mixerPage  { push::Icon::pageMixer, "Mixer" };

    int currentPage = 3;  // TransportBar::pageDevice

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageHost)
};

} // namespace conduit
