#pragma once

#include <string>

// ─────────────────────────────────────────────
//  executor.hpp : non-blocking command spawner
// ─────────────────────────────────────────────

namespace pf {

class Executor {
public:
    // Fire-and-forget: fork + exec "/bin/sh -c cmd".
    // Returns the child PID, or -1 on failure.
    // The caller does NOT wait; SIGCHLD → SIG_IGN prevents zombies.
    static int run(const std::string& cmd);
};

} // namespace pf
