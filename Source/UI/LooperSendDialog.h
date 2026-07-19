#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    Send-Routing-Dialog eines Looper-Tracks (Big Out): Schalter S1–S4
    (Bitmaske) + PRE/POST-Abgriff des Tracks. Zustand kommt per Setter,
    Änderungen laufen als Hook nach oben (der Editor persistiert in die
    LooperSettings, Ein-Pfad-Regel) — der Dialog bleibt offen, Tap
    außerhalb schließt die CallOutBox.

    Kein Modal-Loop (13.2): wird per CallOutBox angezeigt. Controls
    public für headless Tests.
*/
class LooperSendDialog final : public juce::Component
{
public:
    static constexpr int numSends = 4;

    /** title z. B. „Looper 2 · Track 3". */
    LooperSendDialog (const juce::String& title, int sendMask, bool sendPre);

    std::function<void (int sendIndex, bool enabled)> onSendToggled;   // 0-basiert
    std::function<void (bool pre)> onPreToggled;

    [[nodiscard]] int getSendMask() const noexcept { return mask; }
    [[nodiscard]] bool isPre() const noexcept { return pre; }

    void resized() override;

    // public für Tests (expliziter TextTile-Ctor → Einzel-Member statt Array)
    push::TextTile send1 { "S1", push::colours::ledGreen };
    push::TextTile send2 { "S2", push::colours::ledGreen };
    push::TextTile send3 { "S3", push::colours::ledGreen };
    push::TextTile send4 { "S4", push::colours::ledGreen };
    push::TextTile preTile { "PRE", push::colours::ledOrange };

    [[nodiscard]] push::TextTile& sendTile (int sendIndex) noexcept
    {
        return *sendTiles[(size_t) juce::jlimit (0, numSends - 1, sendIndex)];
    }

private:
    void refreshTiles();

    push::TextTile* sendTiles[numSends] { &send1, &send2, &send3, &send4 };

    juce::Label titleLabel;
    int mask = 0;
    bool pre = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperSendDialog)
};

} // namespace conduit
