#include "event.hpp"
#include "mapper.hpp"
#include "executor.hpp"
#include "common.hpp"

#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <chrono>

// ─────────────────────────────────────────────
//  event.cpp
// ─────────────────────────────────────────────

namespace pf {

// ── Internal helpers ─────────────────────────

static uint64_t now_us()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

// ── EventLoop::run ───────────────────────────

void EventLoop::run()
{
    Mapper mapper;
    mapper.init();

    std::cout << "[event] Loop started. Listening…\n";

    while (g_running) {
        InputEvent ev{};
        if (!read_next(ev)) {
            std::cerr << "[event] Device read error – stopping loop.\n";
            g_running = false;
            break;
        }

        // We only act on key-press events (value == 1).
        // Releases (0) and repeats (2) are ignored.
        if (ev.value != EV_KEY_PRESS)
            continue;

        if (g_config.debug) {
            std::cout << "[event] KEY " << Mapper::code_to_name(ev.code)
                      << " (" << ev.code << ") pressed\n";
        }

        auto cmd = mapper.lookup(ev.code);
        if (!cmd.has_value()) {
            if (g_config.debug)
                std::cout << "[event] No binding for code " << ev.code << "\n";
            continue;
        }

        std::cout << "[event] Executing: " << *cmd << "\n";
        Executor::run(*cmd);
    }

    std::cout << "[event] Loop exited.\n";
}

// ── EventLoop::read_next ──────────────────────

bool EventLoop::read_next(InputEvent& out)
{
    struct input_event ev{};
    int rc;

    // LIBEVDEV_READ_FLAG_NORMAL blocks until an event is available.
    // Returns EAGAIN when no data (non-blocking fd), EIO on disconnect.
    while (g_running) {
        rc = libevdev_next_event(device_.dev(),
                                 LIBEVDEV_READ_FLAG_NORMAL |
                                 LIBEVDEV_READ_FLAG_BLOCKING,
                                 &ev);

        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            // We only care about EV_KEY events.
            if (ev.type != EV_KEY)
                continue;

            out.code      = ev.code;
            out.value     = ev.value;
            out.timestamp = now_us();
            return true;
        }

        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            // SYN_DROPPED: drain sync events and continue.
            while ((rc = libevdev_next_event(
                        device_.dev(), LIBEVDEV_READ_FLAG_SYNC, &ev))
                   == LIBEVDEV_READ_STATUS_SYNC) {
                // drain
            }
            continue;
        }

        if (rc == -EAGAIN)
            continue; // no event yet (shouldn't happen with BLOCKING)

        // EIO or other fatal error → device disconnected
        std::cerr << "[event] libevdev error: " << std::strerror(-rc) << "\n";
        return false;
    }

    return false; // g_running became false
}

} // namespace pf
