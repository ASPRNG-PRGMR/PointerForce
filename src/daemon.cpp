#include "daemon.hpp"
#include "common.hpp"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <iostream>

// ─────────────────────────────────────────────
//  daemon.cpp
// ─────────────────────────────────────────────

namespace pf {

// ── Signal handler ───────────────────────────

static void handle_signal(int /*sig*/)
{
    g_running = false;
}

void Daemon::install_signal_handlers()
{
    struct sigaction sa{};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGHUP,  &sa, nullptr);
}

// ── Daemonise ────────────────────────────────

bool Daemon::daemonize()
{
    // First fork
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[daemon] First fork failed: "
                  << std::strerror(errno) << "\n";
        return false;
    }
    if (pid > 0)
        std::exit(0); // parent exits

    // New session
    if (setsid() < 0) {
        std::cerr << "[daemon] setsid failed: "
                  << std::strerror(errno) << "\n";
        return false;
    }

    // Second fork – prevent re-acquiring a controlling terminal
    pid = fork();
    if (pid < 0) {
        std::cerr << "[daemon] Second fork failed: "
                  << std::strerror(errno) << "\n";
        return false;
    }
    if (pid > 0)
        std::exit(0);

    // File permissions mask
    umask(027);

    // Redirect stdio to /dev/null
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO)
            close(devnull);
    }

    return true;
}

// ── PID file ─────────────────────────────────

bool Daemon::write_pid()
{
    std::ofstream f(PID_FILE);
    if (!f.is_open()) {
        std::cerr << "[daemon] Cannot write PID file: "
                  << PID_FILE << "\n";
        return false;
    }
    f << getpid() << "\n";
    return true;
}

void Daemon::remove_pid()
{
    ::unlink(PID_FILE);
}

bool Daemon::is_running()
{
    std::ifstream f(PID_FILE);
    if (!f.is_open()) return false;

    pid_t pid = 0;
    f >> pid;
    if (pid <= 0) return false;

    // kill(pid, 0) checks if process exists without sending a signal
    if (kill(pid, 0) == 0) return true;
    if (errno == EPERM)   return true;  // exists but we lack permission

    // Stale PID file
    remove_pid();
    return false;
}

} // namespace pf
