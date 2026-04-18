#pragma once

// ─────────────────────────────────────────────
//  daemon.hpp : daemonise, PID file, lifecycle
// ─────────────────────────────────────────────

namespace pf {

class Daemon {
public:
    // Fork into background, redirect stdio, set new session.
    static bool daemonize();

    // Write our PID to PID_FILE.
    static bool write_pid();

    // Remove PID_FILE on clean shutdown.
    static void remove_pid();

    // Return true if another instance is already running.
    static bool is_running();

    // Install signal handlers:
    //   SIGTERM / SIGINT → g_running = false
    //   SIGHUP           → g_reload  = true  (in-process config reload)
    static void install_signal_handlers();
};

} // namespace pf
