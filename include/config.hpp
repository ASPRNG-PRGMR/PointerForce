#pragma once

#include "common.hpp"
#include <string>

// ─────────────────────────────────────────────
//  config.hpp : load & validate pointerforce.json
// ─────────────────────────────────────────────

namespace pf {

class ConfigLoader {
public:
    // Load JSON from path into g_config.
    // Clears and replaces all existing config (safe to call for hot-reload).
    // Returns true on success.
    static bool load(const std::string& path);

    // Validate the already-loaded g_config (at least one device, etc.)
    static bool validate();

    // Dump current config to stdout.
    static void dump();
};

} // namespace pf
