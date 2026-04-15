#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <cstdint>

// ─────────────────────────────────────────────
//  PointerForce – Phase 1
//  common.hpp : shared types, constants, globals
// ─────────────────────────────────────────────

namespace pf {

// ── Version ──────────────────────────────────
constexpr const char* VERSION     = "0.1.0";
constexpr const char* PID_FILE    = "/tmp/pointerforce.pid";
constexpr const char* LOG_FILE    = "logs/pointerforce.log";
constexpr const char* CONFIG_FILE = "config/pointerforce.json";

// ── Input event values (matches Linux input.h) ──
constexpr int EV_KEY_RELEASE = 0;
constexpr int EV_KEY_PRESS   = 1;
constexpr int EV_KEY_REPEAT  = 2;

// ── Runtime config ───────────────────────────
struct Config {
    std::string device_path;              // e.g. /dev/input/event5
    bool        grab       = false;       // exclusive grab via ioctl
    bool        daemon_mode = false;      // run as background daemon
    bool        debug      = false;       // verbose logging
    std::unordered_map<int, std::string> bindings; // keycode → shell command
};

// ── Lightweight event snapshot ────────────────
struct InputEvent {
    int      code;       // Linux key code (KEY_*)
    int      value;      // 0=release, 1=press, 2=repeat
    uint64_t timestamp;  // microseconds since epoch
};

// ── Global runtime state (set once at startup) ──
extern Config      g_config;
extern bool        g_running;  // set false to break the event loop

} // namespace pf
