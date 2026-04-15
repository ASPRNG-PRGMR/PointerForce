#include "config.hpp"
#include "mapper.hpp"

#include <json/json.h>
#include <fstream>
#include <iostream>
#include <filesystem>

// ─────────────────────────────────────────────
//  config.cpp
// ─────────────────────────────────────────────

namespace pf {

bool ConfigLoader::load(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[config] Cannot open: " << path << "\n";
        return false;
    }

    Json::Value  root;
    Json::Reader reader;
    if (!reader.parse(file, root)) {
        std::cerr << "[config] JSON parse error: "
                  << reader.getFormattedErrorMessages() << "\n";
        return false;
    }

    // ── device ──────────────────────────────
    if (!root.isMember("device") || !root["device"].isString()) {
        std::cerr << "[config] Missing or invalid 'device' field.\n";
        return false;
    }
    g_config.device_path = root["device"].asString();

    // ── grab (optional, default false) ──────
    if (root.isMember("grab") && root["grab"].isBool())
        g_config.grab = root["grab"].asBool();

    // ── daemon (optional, default false) ────
    if (root.isMember("daemon") && root["daemon"].isBool())
        g_config.daemon_mode = root["daemon"].asBool();

    // ── debug (optional, default false) ─────
    if (root.isMember("debug") && root["debug"].isBool())
        g_config.debug = root["debug"].asBool();

    // ── bindings ────────────────────────────
    if (!root.isMember("bindings") || !root["bindings"].isObject()) {
        std::cerr << "[config] No 'bindings' object found.\n";
        return false;
    }

    const Json::Value& bindings = root["bindings"];
    for (const auto& key_name : bindings.getMemberNames()) {
        int code = Mapper::name_to_code(key_name);
        if (code < 0) {
            std::cerr << "[config] Unknown key name: " << key_name
                      << " – skipping.\n";
            continue;
        }
        g_config.bindings[code] = bindings[key_name].asString();
    }

    return true;
}

bool ConfigLoader::validate()
{
    if (g_config.device_path.empty()) {
        std::cerr << "[config] device_path is empty.\n";
        return false;
    }
    if (!std::filesystem::exists(g_config.device_path)) {
        std::cerr << "[config] Device not found: "
                  << g_config.device_path << "\n";
        return false;
    }
    if (g_config.bindings.empty()) {
        std::cerr << "[config] Warning: no bindings configured.\n";
        // Not fatal – user might just want to test device detection.
    }
    return true;
}

void ConfigLoader::dump()
{
    std::cout << "[config] device     : " << g_config.device_path << "\n"
              << "[config] grab       : " << (g_config.grab        ? "true" : "false") << "\n"
              << "[config] daemon     : " << (g_config.daemon_mode ? "true" : "false") << "\n"
              << "[config] debug      : " << (g_config.debug       ? "true" : "false") << "\n"
              << "[config] bindings   : " << g_config.bindings.size() << " entries\n";
    for (const auto& [code, cmd] : g_config.bindings) {
        std::cout << "           "
                  << Mapper::code_to_name(code)
                  << " (" << code << ") → " << cmd << "\n";
    }
}

} // namespace pf
