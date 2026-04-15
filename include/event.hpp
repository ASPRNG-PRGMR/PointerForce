#pragma once

#include "device.hpp"
#include "common.hpp"

// ─────────────────────────────────────────────
//  event.hpp : main event loop (EV_KEY reader)
// ─────────────────────────────────────────────

namespace pf {

class EventLoop {
public:
    explicit EventLoop(Device& device) : device_(device) {}

    // Blocking loop – returns when g_running becomes false
    // or the device becomes unreadable (disconnect).
    void run();

private:
    Device& device_;

    // Returns false when the device should be considered disconnected.
    bool read_next(InputEvent& out);
};

} // namespace pf
