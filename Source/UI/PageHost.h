#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PushIcons.h"

namespace conduit
{

//==============================================================================
/**
    Host der vier Pages hinter den Push-Icons der TransportBar
    (User-Entscheidung 2026-07-02):

      Grid   (Ω)   — AbletonOSC-Remote-Page        [eigener Meilenstein]
      Mixer  (∥∥)  — Mixer-Page                    [eigener Meilenstein]
      Clip   (▷▭)  — Fugue-Machine-Sequencer,
                     CV- und MIDI-Ziele            [eigener Meilenstein]
      Device (|||) — die Patch-Canvas (bestehend)
      Looper (oo)  — Retro-Looper (B3), erreichbar über die Tape-Kachel
                     statt über ein Page-Icon

    Der Host besitzt nur die Platzhalter; Device-Komponente (NodeCanvas)
    und LooperPage gehören weiterhin dem EngineEditor und werden als
    Referenz eingehängt — ihre Verdrahtung bleibt unangetastet.

    Page-Indizes == TransportBar::PageIndex. Message Thread.
*/
class PageHost final : public juce::Component
{
public:
    /** devicePage (Canvas) und looperPage: nicht owned, müssen den Host
        überleben — Member-Reihenfolge im EngineEditor. */
    PageHost (juce::Component& devicePage, juce::Component& looperPage);

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
    Placeholder gridPage   { push::Icon::pageGrid,  "Grid" };
    Placeholder mixerPage  { push::Icon::pageMixer, "Mixer" };
    Placeholder clipPage   { push::Icon::pageClip,  "Clip" };

    int currentPage = 3;  // TransportBar::pageDevice

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageHost)
};

} // namespace conduit
