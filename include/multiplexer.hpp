#pragma once

#include "eventqueue.hpp"
#include "mapper.hpp"
#include "common.hpp"

#include <functional>
#include <map>
#include <mutex>

// ─────────────────────────────────────────────
//  multiplexer.hpp : central event dispatcher
//
//  Pops events from the shared EventQueue, performs a device-aware
//  binding lookup, and fires commands via Executor.
//
//  Registered EventObservers (e.g. ControlServer event-stream clients)
//  are notified on every key-press, bound or not.
// ─────────────────────────────────────────────

namespace pf {

// Observer signature: (event, bound_command_or_empty)
using EventObserver = std::function<void(const InputEvent&, const std::string&)>;

class EventMultiplexer {
public:
    EventMultiplexer(EventQueue& queue, Mapper& mapper)
        : queue_(queue), mapper_(mapper) {}

    // Blocking – returns when g_running becomes false.
    void run();

    // Register an observer; returns an opaque id for later removal.
    int  add_observer(EventObserver obs);
    void remove_observer(int id);

private:
    EventQueue& queue_;
    Mapper&     mapper_;

    std::map<int, EventObserver> observers_;
    int                          next_obs_id_ = 0;
    std::mutex                   obs_mu_;

    void notify(const InputEvent& ev, const std::string& cmd);
};

} // namespace pf
