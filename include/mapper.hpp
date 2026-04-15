#pragma once

#include "common.hpp"
#include <string>
#include <optional>

// ─────────────────────────────────────────────
//  mapper.hpp : keycode → command lookup
// ─────────────────────────────────────────────

namespace pf {

class Mapper {
public:
    // Initialise from g_config.bindings (call after config is loaded).
    void init();

    // Return the command string for keycode, or nullopt if unmapped.
    std::optional<std::string> lookup(int keycode) const;

    // Convert a human-friendly key name ("KEY_A", "BTN_LEFT", …) to
    // its Linux key code.  Returns -1 on failure.
    static int name_to_code(const std::string& name);

    // Inverse: code → "KEY_*" string (for logging).
    static std::string code_to_name(int code);

private:
    // Populated from g_config.bindings; kept here for fast O(1) lookup.
    // Key: Linux keycode   Value: shell command string
    std::unordered_map<int, std::string> table_;
};

} // namespace pf
