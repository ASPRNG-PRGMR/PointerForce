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
    // Returns true on success, false on any parse error.
    static bool load(const std::string& path);

    // Validate the already-loaded g_config (device path exists, etc.)
    static bool validate();

    // Dump current config to stdout (debug helper)
    static void dump();
};

} // namespace pf
