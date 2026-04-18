#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <cstdint>
#include <atomic>

// ─────────────────────────────────────────────
//  PointerForce – Phase 2 / 2.5
//  common.hpp : shared types, constants, globals
// ─────────────────────────────────────────────

namespace pf {

// ── Version ──────────────────────────────────
constexpr const char* VERSION        = "0.2.0";
constexpr const char* PID_FILE       = "/tmp/pointerforce.pid";
constexpr const char* LOG_FILE       = "logs/pointerforce.log";
constexpr const char* CONFIG_FILE    = "config/pointerforce.json";
constexpr const char* CONTROL_SOCKET = "/tmp/pointerforce.sock";

// ── Input event values (matches Linux input.h) ──
constexpr int EV_KEY_RELEASE = 0;
constexpr int EV_KEY_PRESS   = 1;
constexpr int EV_KEY_REPEAT  = 2;

// ── Device match criteria ─────────────────────
// First non-empty field that matches a physical device wins.
struct DeviceMatch {
    std::string name_contains;    // substring of libevdev device name
    std::string vendor_product;   // "VVVV:PPPP" lowercase hex (sysfs ids)
    std::string path;             // exact /dev/input/eventN path
};

// ── Per-device configuration block ───────────
struct DeviceConfig {
    std::string id;               // logical label  ("gaming_mouse", …)
    DeviceMatch match;
    bool        grab = false;     // exclusive grab
    std::unordered_map<int, std::string> bindings; // keycode → shell cmd
};

// ── Global runtime configuration ─────────────
struct Config {
    std::vector<DeviceConfig> devices;
    bool        daemon_mode    = false;
    bool        debug          = false;
    std::string control_socket = CONTROL_SOCKET;
    std::string config_path    = CONFIG_FILE;     // remembered for reload
};

// ── Device-tagged input event ─────────────────
struct InputEvent {
    std::string device_id;    // logical label from DeviceConfig
    std::string device_path;  // /dev/input/eventN
    int         code;         // Linux key / button code
    int         value;        // 0=release  1=press  2=repeat
    uint64_t    timestamp;    // µs (steady_clock since epoch)
};

// ── Global runtime state ──────────────────────
extern Config              g_config;
extern std::atomic<bool>   g_running;   // false → stop event loop
extern std::atomic<bool>   g_reload;    // true  → reload config in-place

} // namespace pf
