#include "common.hpp"
#include "config.hpp"
#include "daemon.hpp"
#include "mapper.hpp"
#include "devicemanager.hpp"
#include "eventqueue.hpp"
#include "multiplexer.hpp"
#include "control.hpp"

#include <iostream>
#include <execinfo.h>
#include <csignal>
#include <cstring>
#include <cstdlib>

// ─────────────────────────────────────────────
//  main.cpp – PointerForce entry point
//
//  Flow:
//    1.  Parse CLI flags
//    2.  Load & validate config
//    3.  Single-instance guard
//    4.  Daemonise (optional)
//    5.  Install signal handlers
//    6.  Open all matched devices
//    7.  Start control server (Phase 2.5)
//    8.  Start device reader threads
//    9.  Run event multiplexer (blocking)
//   10.  Clean shutdown
// ─────────────────────────────────────────────

// ── Global definitions ────────────────────────
namespace pf {
    Config             g_config;
    std::atomic<bool>  g_running{true};
    std::atomic<bool>  g_reload{false};
}

static void usage(const char* argv0)
{
    std::cout
        << "PointerForce " << pf::VERSION << "\n"
        << "Usage: " << argv0 << " [options]\n\n"
        << "  -c <file>   Config file  (default: " << pf::CONFIG_FILE << ")\n"
        << "  -d          Run as daemon (overrides config)\n"
        << "  -f          Foreground / debug mode (overrides config)\n"
        << "  -v          Print version and exit\n"
        << "  -h          Show this help\n\n"
        << "Control socket (default: " << pf::CONTROL_SOCKET << ")\n"
        << "  pfctl status | devices | bindings | events\n"
        << "  pfctl bind   <device> <KEY> <command>\n"
        << "  pfctl unbind <device> <KEY>\n"
        << "  pfctl reload\n";
}

static void crash_handler(int sig)
{
    void* buf[32];
    int n = backtrace(buf, 32);
    fprintf(stderr, "\n[crash] Signal %d. Backtrace:\n", sig);
    backtrace_symbols_fd(buf, n, STDERR_FILENO);
    _exit(1);
}

int main(int argc, char** argv)
{
    std::string config_path    = pf::CONFIG_FILE;
    bool        cli_daemon     = false;
    bool        cli_foreground = false;

    // ── 1. CLI flags ─────────────────────────
    for (int i = 1; i < argc; ++i) {
        if      (std::strcmp(argv[i], "-h") == 0) { usage(argv[0]); return 0; }
        else if (std::strcmp(argv[i], "-v") == 0) {
            std::cout << "PointerForce " << pf::VERSION << "\n";
            return 0;
        }
        else if (std::strcmp(argv[i], "-d") == 0) { cli_daemon     = true; }
        else if (std::strcmp(argv[i], "-f") == 0) { cli_foreground = true; }
        else if (std::strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        }
        else {
            std::cerr << "[main] Unknown option: " << argv[i] << "\n";
            usage(argv[0]);
            return 1;
        }
    }

    // Install crash handler early so segfaults print a backtrace
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);

    // ── 2. Load & validate config ─────────────
    pf::g_config.config_path = config_path;
    if (!pf::ConfigLoader::load(config_path)) {
        std::cerr << "[main] Failed to load config.\n";
        return 1;
    }

    if (cli_daemon)     pf::g_config.daemon_mode = true;
    if (cli_foreground) { pf::g_config.daemon_mode = false;
                          pf::g_config.debug       = true; }

    if (pf::g_config.debug) pf::ConfigLoader::dump();

    if (!pf::ConfigLoader::validate()) return 1;

    // ── 3. Single-instance guard ──────────────
    if (pf::Daemon::is_running()) {
        std::cerr << "[main] PointerForce is already running.\n";
        return 1;
    }

    // ── 4. Daemonise ─────────────────────────
    if (pf::g_config.daemon_mode) {
        if (!pf::Daemon::daemonize()) return 1;
        if (!pf::Daemon::write_pid()) return 1;
        (void)freopen(pf::LOG_FILE, "a", stdout);
        (void)freopen(pf::LOG_FILE, "a", stderr);
    }

    // ── 5. Signal handlers ────────────────────
    pf::Daemon::install_signal_handlers();

    std::cout << "[main] PointerForce " << pf::VERSION << " starting.\n";

    // ── 6. Initialise subsystems ──────────────
    pf::EventQueue      queue;
    pf::Mapper          mapper;
    mapper.init();

    pf::DeviceManager   dm(queue);
    int opened = dm.open_all();
    if (opened == 0) {
        std::cerr << "[main] No devices could be opened. "
                  << "Check config and 'input' group membership.\n";
        pf::Daemon::remove_pid();
        return 1;
    }

    pf::EventMultiplexer mux(queue, mapper);

    // ── 7. Control server ─────────────────────
    pf::ControlServer ctrl(mapper, dm, mux);
    if (!ctrl.start())
        std::cerr << "[main] Warning: control server failed to start.\n";

    // ── 8. Start device reader threads ───────
    dm.start();

    std::cout << "[main] Running with " << opened << " device(s).\n";

    // ── 9. Event multiplexer (blocking) ───────
    mux.run();

    // ── 10. Shutdown ─────────────────────────
    ctrl.stop();
    dm.stop();
    queue.shutdown();
    pf::Daemon::remove_pid();

    std::cout << "[main] Shutdown complete.\n";
    return 0;
}
