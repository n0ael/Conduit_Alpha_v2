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

    Der Host besitzt nur die Platzhalter; die Device-Komponente (NodeCanvas)
    gehört weiterhin dem EngineEditor und wird als Referenz eingehängt —
    ihre Verdrahtung (Provider, Tooltips) bleibt unangetastet.

    Page-Indizes == TransportBar::PageIndex. Message Thread.
*/
class PageHost final : public juce::Component
{
public:
    /** devicePage: die bestehende Canvas (nicht owned, muss den Host
        überleben — Member-Reihenfolge im EngineEditor). */
    explicit PageHost (juce::Component& devicePage);

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
    Placeholder gridPage   { push::Icon::pageGrid,  "Grid" };
    Placeholder mixerPage  { push::Icon::pageMixer, "Mixer" };
    Placeholder clipPage   { push::Icon::pageClip,  "Clip" };

    int currentPage = 3;  // TransportBar::pageDevice

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageHost)
};

} // namespace conduit
