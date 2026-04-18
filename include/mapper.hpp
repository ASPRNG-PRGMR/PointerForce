#pragma once

#include "common.hpp"
#include <string>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────
//  mapper.hpp : device-aware keycode → command lookup
//
//  Thread-safe: readers (lookup) take a shared lock;
//  writers (add/remove binding, init) take an exclusive lock.
//  The wildcard device id "*" matches any device.
// ─────────────────────────────────────────────

namespace pf {

class Mapper {
public:
    // Load bindings from all DeviceConfig entries in g_config.
    // Safe to call as a hot-reload (replaces all existing bindings).
    void init();

    // Return the command for (device_id, keycode).
    // Falls back to the wildcard device "*" if no exact match exists.
    // Returns nullopt if unmapped on both.
    std::optional<std::string> lookup(const std::string& device_id,
                                      int keycode) const;

    // ── Runtime binding modification ─────────
    // add/replace a binding; returns false if key_name is unknown.
    bool add_binding(const std::string& device_id,
                     const std::string& key_name,
                     const std::string& cmd);

    // Remove a binding; returns false if it did not exist.
    bool remove_binding(const std::string& device_id,
                        const std::string& key_name);

    // ── Enumeration (for control/status) ────
    struct BindingEntry {
        std::string device_id;
        std::string key_name;
        int         keycode;
        std::string command;
    };
    std::vector<BindingEntry> all_bindings() const;

    // ── Static key name ↔ code translation ──
    static int         name_to_code(const std::string& name);
    static std::string code_to_name(int code);

private:
    // table_[device_id][keycode] = command
    std::unordered_map<std::string,
        std::unordered_map<int, std::string>> table_;
    mutable std::shared_mutex mu_;
};

} // namespace pf
