#include "mapper.hpp"
#include "common.hpp"

#include <linux/input-event-codes.h>
#include <iostream>
#include <mutex>

// ─────────────────────────────────────────────
//  mapper.cpp
// ─────────────────────────────────────────────

namespace pf {

// ── Static name ↔ code tables ────────────────

static const std::unordered_map<std::string, int>& name_table()
{
    static const std::unordered_map<std::string, int> t = {
        // ── Alphabet ────────────────────────
        {"KEY_A",KEY_A},{"KEY_B",KEY_B},{"KEY_C",KEY_C},{"KEY_D",KEY_D},
        {"KEY_E",KEY_E},{"KEY_F",KEY_F},{"KEY_G",KEY_G},{"KEY_H",KEY_H},
        {"KEY_I",KEY_I},{"KEY_J",KEY_J},{"KEY_K",KEY_K},{"KEY_L",KEY_L},
        {"KEY_M",KEY_M},{"KEY_N",KEY_N},{"KEY_O",KEY_O},{"KEY_P",KEY_P},
        {"KEY_Q",KEY_Q},{"KEY_R",KEY_R},{"KEY_S",KEY_S},{"KEY_T",KEY_T},
        {"KEY_U",KEY_U},{"KEY_V",KEY_V},{"KEY_W",KEY_W},{"KEY_X",KEY_X},
        {"KEY_Y",KEY_Y},{"KEY_Z",KEY_Z},
        // ── Digits ──────────────────────────
        {"KEY_0",KEY_0},{"KEY_1",KEY_1},{"KEY_2",KEY_2},{"KEY_3",KEY_3},
        {"KEY_4",KEY_4},{"KEY_5",KEY_5},{"KEY_6",KEY_6},{"KEY_7",KEY_7},
        {"KEY_8",KEY_8},{"KEY_9",KEY_9},
        // ── Function keys ───────────────────
        {"KEY_F1",KEY_F1},  {"KEY_F2",KEY_F2},  {"KEY_F3",KEY_F3},
        {"KEY_F4",KEY_F4},  {"KEY_F5",KEY_F5},  {"KEY_F6",KEY_F6},
        {"KEY_F7",KEY_F7},  {"KEY_F8",KEY_F8},  {"KEY_F9",KEY_F9},
        {"KEY_F10",KEY_F10},{"KEY_F11",KEY_F11},{"KEY_F12",KEY_F12},
        {"KEY_F13",KEY_F13},{"KEY_F14",KEY_F14},{"KEY_F15",KEY_F15},
        {"KEY_F16",KEY_F16},{"KEY_F17",KEY_F17},{"KEY_F18",KEY_F18},
        {"KEY_F19",KEY_F19},{"KEY_F20",KEY_F20},
        // ── Navigation ──────────────────────
        {"KEY_UP",KEY_UP},{"KEY_DOWN",KEY_DOWN},
        {"KEY_LEFT",KEY_LEFT},{"KEY_RIGHT",KEY_RIGHT},
        {"KEY_HOME",KEY_HOME},{"KEY_END",KEY_END},
        {"KEY_PAGEUP",KEY_PAGEUP},{"KEY_PAGEDOWN",KEY_PAGEDOWN},
        {"KEY_INSERT",KEY_INSERT},{"KEY_DELETE",KEY_DELETE},
        // ── Common keys ─────────────────────
        {"KEY_ENTER",KEY_ENTER},{"KEY_SPACE",KEY_SPACE},
        {"KEY_TAB",KEY_TAB},{"KEY_BACKSPACE",KEY_BACKSPACE},
        {"KEY_ESC",KEY_ESC},{"KEY_CAPSLOCK",KEY_CAPSLOCK},
        {"KEY_LEFTSHIFT",KEY_LEFTSHIFT},{"KEY_RIGHTSHIFT",KEY_RIGHTSHIFT},
        {"KEY_LEFTCTRL",KEY_LEFTCTRL},{"KEY_RIGHTCTRL",KEY_RIGHTCTRL},
        {"KEY_LEFTALT",KEY_LEFTALT},{"KEY_RIGHTALT",KEY_RIGHTALT},
        {"KEY_LEFTMETA",KEY_LEFTMETA},{"KEY_RIGHTMETA",KEY_RIGHTMETA},
        // ── Media / extra ───────────────────
        {"KEY_MUTE",KEY_MUTE},
        {"KEY_VOLUMEUP",KEY_VOLUMEUP},{"KEY_VOLUMEDOWN",KEY_VOLUMEDOWN},
        {"KEY_PLAYPAUSE",KEY_PLAYPAUSE},
        {"KEY_NEXTSONG",KEY_NEXTSONG},{"KEY_PREVIOUSSONG",KEY_PREVIOUSSONG},
        {"KEY_PRINT",KEY_PRINT},{"KEY_SYSRQ",KEY_SYSRQ},
        {"KEY_PAUSE",KEY_PAUSE},{"KEY_SCROLLLOCK",KEY_SCROLLLOCK},
        {"KEY_NUMLOCK",KEY_NUMLOCK},
        // ── Numpad ──────────────────────────
        {"KEY_KP0",KEY_KP0},{"KEY_KP1",KEY_KP1},{"KEY_KP2",KEY_KP2},
        {"KEY_KP3",KEY_KP3},{"KEY_KP4",KEY_KP4},{"KEY_KP5",KEY_KP5},
        {"KEY_KP6",KEY_KP6},{"KEY_KP7",KEY_KP7},{"KEY_KP8",KEY_KP8},
        {"KEY_KP9",KEY_KP9},{"KEY_KPENTER",KEY_KPENTER},
        {"KEY_KPPLUS",KEY_KPPLUS},{"KEY_KPMINUS",KEY_KPMINUS},
        {"KEY_KPASTERISK",KEY_KPASTERISK},{"KEY_KPSLASH",KEY_KPSLASH},
        {"KEY_KPDOT",KEY_KPDOT},
        // ── Mouse / pointer buttons ──────────
        {"BTN_LEFT",BTN_LEFT},{"BTN_RIGHT",BTN_RIGHT},
        {"BTN_MIDDLE",BTN_MIDDLE},{"BTN_SIDE",BTN_SIDE},
        {"BTN_EXTRA",BTN_EXTRA},{"BTN_FORWARD",BTN_FORWARD},
        {"BTN_BACK",BTN_BACK},
        // ── Gamepad buttons ─────────────────
        {"BTN_SOUTH",BTN_SOUTH},{"BTN_EAST",BTN_EAST},
        {"BTN_NORTH",BTN_NORTH},{"BTN_WEST",BTN_WEST},
        {"BTN_TL",BTN_TL},  {"BTN_TR",BTN_TR},
        {"BTN_TL2",BTN_TL2},{"BTN_TR2",BTN_TR2},
        {"BTN_START",BTN_START},{"BTN_SELECT",BTN_SELECT},
        {"BTN_THUMBL",BTN_THUMBL},{"BTN_THUMBR",BTN_THUMBR},
        {"BTN_MODE",BTN_MODE},
    };
    return t;
}

static const std::unordered_map<int, std::string>& code_table()
{
    static std::unordered_map<int, std::string> t;
    static bool built = false;
    if (!built) {
        for (const auto& [name, code] : name_table())
            t.emplace(code, name);
        built = true;
    }
    return t;
}

// ── Mapper ───────────────────────────────────

void Mapper::init()
{
    std::unique_lock<std::shared_mutex> lock(mu_);
    table_.clear();

    for (const auto& dc : g_config.devices) {
        auto& sub = table_[dc.id];
        for (const auto& [code, cmd] : dc.bindings)
            sub[code] = cmd;
    }

    if (g_config.debug) {
        int total = 0;
        for (const auto& [id, sub] : table_)
            total += static_cast<int>(sub.size());
        std::cout << "[mapper] Loaded " << total
                  << " binding(s) across " << table_.size()
                  << " device(s).\n";
    }
}

std::optional<std::string> Mapper::lookup(const std::string& device_id,
                                           int keycode) const
{
    std::shared_lock<std::shared_mutex> lock(mu_);

    // 1. Exact device match.
    auto dit = table_.find(device_id);
    if (dit != table_.end()) {
        auto kit = dit->second.find(keycode);
        if (kit != dit->second.end())
            return kit->second;
    }

    // 2. Wildcard fallback ("*" matches any device).
    auto wit = table_.find("*");
    if (wit != table_.end()) {
        auto kit = wit->second.find(keycode);
        if (kit != wit->second.end())
            return kit->second;
    }

    return std::nullopt;
}

bool Mapper::add_binding(const std::string& device_id,
                          const std::string& key_name,
                          const std::string& cmd)
{
    int code = name_to_code(key_name);
    if (code < 0) return false;

    std::unique_lock<std::shared_mutex> lock(mu_);
    table_[device_id][code] = cmd;
    return true;
}

bool Mapper::remove_binding(const std::string& device_id,
                             const std::string& key_name)
{
    int code = name_to_code(key_name);
    if (code < 0) return false;

    std::unique_lock<std::shared_mutex> lock(mu_);
    auto dit = table_.find(device_id);
    if (dit == table_.end()) return false;
    return dit->second.erase(code) > 0;
}

std::vector<Mapper::BindingEntry> Mapper::all_bindings() const
{
    std::shared_lock<std::shared_mutex> lock(mu_);
    std::vector<BindingEntry> result;
    for (const auto& [dev_id, sub] : table_) {
        for (const auto& [code, cmd] : sub) {
            result.push_back({ dev_id, code_to_name(code), code, cmd });
        }
    }
    return result;
}

int Mapper::name_to_code(const std::string& name)
{
    const auto& t = name_table();
    auto it = t.find(name);
    return (it == t.end()) ? -1 : it->second;
}

std::string Mapper::code_to_name(int code)
{
    const auto& t = code_table();
    auto it = t.find(code);
    if (it == t.end())
        return "KEY_UNKNOWN(" + std::to_string(code) + ")";
    return it->second;
}

} // namespace pf
