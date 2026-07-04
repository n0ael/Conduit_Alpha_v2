#pragma once

#include <functional>
#include <mutex>
#include <vector>

#include "Core/Browser/BrowserSearchIndex.h"

namespace conduit::test
{

//==============================================================================
/**
    Test-Dispatcher für den BrowserSearchIndex: sammelt die vom Pool-Thread
    dispatchten Ergebnis-Lambdas in einer Queue, die der Test auf dem
    (Test-)Message-Thread selbst pumpt — runDispatchLoopUntil existiert
    mit JUCE_MODAL_LOOPS_PERMITTED=0 nicht.
*/
struct QueueDispatcher
{
    [[nodiscard]] BrowserSearchIndex::Dispatcher fn()
    {
        return [this] (std::function<void()> task)
        {
            const std::lock_guard<std::mutex> lock (mutex);
            queue.push_back (std::move (task));
        };
    }

    /** Pumpt die Queue, bis die Bedingung erfüllt ist (max. timeoutMs). */
    bool pumpUntil (const std::function<bool()>& done, int timeoutMs = 2000)
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;

        while (juce::Time::getMillisecondCounterHiRes() < deadline)
        {
            std::vector<std::function<void()>> pending;
            {
                const std::lock_guard<std::mutex> lock (mutex);
                pending.swap (queue);
            }

            for (auto& task : pending)
                task();

            if (done())
                return true;

            juce::Thread::sleep (5);
        }

        return done();
    }

    std::mutex mutex;
    std::vector<std::function<void()>> queue;
};

} // namespace conduit::test
