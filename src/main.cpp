#include "common.hpp"
#include "config.hpp"
#include "device.hpp"
#include "event.hpp"
#include "daemon.hpp"
#include "mapper.hpp"

#include <iostream>
#include <cstring>
#include <cstdlib>

// ─────────────────────────────────────────────
//  main.cpp – PointerForce entry point
//
//  Flow:
//    1. Parse CLI flags
//    2. Load & validate config
//    3. Check for existing instance
//    4. Optionally daemonise
//    5. Install signal handlers
//    6. Open device (+ optional grab)
//    7. Run event loop
//    8. Clean shutdown
// ─────────────────────────────────────────────

// ── Global definitions (declared extern in common.hpp) ──
namespace pf {
    Config g_config;
    bool   g_running = true;
}

static void usage(const char* argv0)
{
    std::cout
        << "PointerForce " << pf::VERSION << "\n"
        << "Usage: " << argv0 << " [options]\n\n"
        << "  -c <file>   Config file (default: " << pf::CONFIG_FILE << ")\n"
        << "  -d          Run as daemon (overrides config)\n"
        << "  -f          Foreground / debug mode\n"
        << "  -v          Print version and exit\n"
        << "  -h          Show this help\n";
}

int main(int argc, char** argv)
{
    std::string config_path = pf::CONFIG_FILE;
    bool cli_daemon    = false;
    bool cli_foreground = false;

    // ── 1. CLI flags ─────────────────────────
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-h") == 0) { usage(argv[0]); return 0; }
        if (std::strcmp(argv[i], "-v") == 0) {
            std::cout << "PointerForce " << pf::VERSION << "\n";
            return 0;
        }
        if (std::strcmp(argv[i], "-d") == 0) { cli_daemon     = true; continue; }
        if (std::strcmp(argv[i], "-f") == 0) { cli_foreground = true; continue; }
        if (std::strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path = argv[++i];
            continue;
        }
        std::cerr << "[main] Unknown option: " << argv[i] << "\n";
        usage(argv[0]);
        return 1;
    }

    // ── 2. Load & validate config ─────────────
    if (!pf::ConfigLoader::load(config_path)) {
        std::cerr << "[main] Failed to load config: " << config_path << "\n";
        return 1;
    }

    // CLI flags override config values
    if (cli_daemon)      pf::g_config.daemon_mode = true;
    if (cli_foreground) {
        pf::g_config.daemon_mode = false;
        pf::g_config.debug       = true;
    }

    if (pf::g_config.debug)
        pf::ConfigLoader::dump();

    if (!pf::ConfigLoader::validate())
        return 1;

    // ── 3. Single-instance guard ──────────────
    if (pf::Daemon::is_running()) {
        std::cerr << "[main] PointerForce is already running.\n";
        return 1;
    }

    // ── 4. Daemonise (if requested) ───────────
    if (pf::g_config.daemon_mode) {
        if (!pf::Daemon::daemonize())
            return 1;
        if (!pf::Daemon::write_pid())
            return 1;
        // In daemon mode stdout/stderr now go to /dev/null.
        // Redirect them to the log file instead.
        if (freopen(pf::LOG_FILE, "a", stdout) == nullptr ||
            freopen(pf::LOG_FILE, "a", stderr) == nullptr) {
            // Not fatal – just means we lose log output.
        }
    }

    // ── 5. Signal handlers ────────────────────
    pf::Daemon::install_signal_handlers();

    // ── 6. Open device ────────────────────────
    pf::Device device;
    if (!device.open(pf::g_config.device_path)) {
        std::cerr << "[main] Could not open device. "
                  << "Are you in the 'input' group?\n";
        pf::Daemon::remove_pid();
        return 1;
    }

    if (pf::g_config.grab) {
        if (!device.grab()) {
            std::cerr << "[main] Warning: failed to grab device.\n";
            // Non-fatal: continue without exclusive grab.
        }
    }

    std::cout << "[main] PointerForce " << pf::VERSION << " started.\n";

    // ── 7. Event loop ─────────────────────────
    pf::EventLoop loop(device);
    loop.run();

    // ── 8. Clean shutdown ─────────────────────
    device.close();
    pf::Daemon::remove_pid();
    std::cout << "[main] Shutdown complete.\n";
    return 0;
}
