#include "executor.hpp"
#include "common.hpp"

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <iostream>

// ─────────────────────────────────────────────
//  executor.cpp
// ─────────────────────────────────────────────

namespace pf {

int Executor::run(const std::string& cmd)
{
    if (cmd.empty()) {
        std::cerr << "[executor] Empty command – skipping.\n";
        return -1;
    }

    // Ignore SIGCHLD so child zombies are reaped automatically.
    struct sigaction sa{};
    sa.sa_handler = SIG_IGN;
    sa.sa_flags   = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa, nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[executor] fork(): " << std::strerror(errno) << "\n";
        return -1;
    }

    if (pid == 0) {
        // Child: new session, then exec.
        setsid();
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        std::cerr << "[executor/child] execl: " << std::strerror(errno) << "\n";
        _exit(127);
    }

    if (g_config.debug)
        std::cout << "[executor] Spawned PID " << pid
                  << " for: " << cmd << "\n";
    return pid;
}

} // namespace pf
