#pragma once

#include <string>

// ─────────────────────────────────────────────
//  daemon.hpp : daemonise, PID file, lifecycle
// ─────────────────────────────────────────────

namespace pf {

class Daemon {
public:
    // Fork into background, redirect stdio, set new session.
    // Returns false if daemonisation fails (caller should exit).
    static bool daemonize();

    // Write our PID to PID_FILE.  Returns false on failure.
    static bool write_pid();

    // Remove PID_FILE on clean shutdown.
    static void remove_pid();

    // Return true if another instance is already running
    // (PID file exists and process is alive).
    static bool is_running();

    // Install SIGTERM / SIGINT handlers that set g_running = false.
    static void install_signal_handlers();
};

} // namespace pf
