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

// ── Signal handlers ──────────────────────────

static void handle_terminate(int /*sig*/)
{
    g_running = false;
}

static void handle_reload(int /*sig*/)
{
    g_reload = true;
}

void Daemon::install_signal_handlers()
{
    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sa.sa_handler = handle_terminate;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT,  &sa, nullptr);

    sa.sa_handler = handle_reload;
    sigaction(SIGHUP, &sa, nullptr);
}

// ── daemonize ────────────────────────────────

bool Daemon::daemonize()
{
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[daemon] First fork failed: "
                  << std::strerror(errno) << "\n";
        return false;
    }
    if (pid > 0) std::exit(0);

    if (setsid() < 0) {
        std::cerr << "[daemon] setsid failed: "
                  << std::strerror(errno) << "\n";
        return false;
    }

    pid = fork();
    if (pid < 0) {
        std::cerr << "[daemon] Second fork failed: "
                  << std::strerror(errno) << "\n";
        return false;
    }
    if (pid > 0) std::exit(0);

    umask(027);

    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO)
            ::close(devnull);
    }

    return true;
}

// ── PID file ─────────────────────────────────

bool Daemon::write_pid()
{
    std::ofstream f(PID_FILE);
    if (!f.is_open()) {
        std::cerr << "[daemon] Cannot write PID file: " << PID_FILE << "\n";
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

    if (kill(pid, 0) == 0)        return true;
    if (errno == EPERM)           return true;

    remove_pid();
    return false;
}

} // namespace pf
