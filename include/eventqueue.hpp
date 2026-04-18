#pragma once

#include "common.hpp"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>

// ─────────────────────────────────────────────
//  eventqueue.hpp : thread-safe event queue
//  Multiple DeviceReader threads push; EventMultiplexer pops.
// ─────────────────────────────────────────────

namespace pf {

class EventQueue {
public:
    // Push an event (safe from any thread).
    void push(InputEvent ev);

    // Block until an event is available, the queue is shut down,
    // or `timeout` elapses.  Returns nullopt on timeout / shutdown.
    std::optional<InputEvent> pop(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(200));

    // Permanently unblock all waiting pop() calls.
    void shutdown();

private:
    std::queue<InputEvent>  q_;
    std::mutex              mu_;
    std::condition_variable cv_;
    bool                    shutdown_ = false;
};

} // namespace pf
