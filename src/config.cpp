#include "config.hpp"
#include "mapper.hpp"

#include <json/json.h>
#include <fstream>
#include <iostream>

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

    // Clear existing state so this function is safe to call as a reload.
    g_config.devices.clear();
    g_config.config_path = path;

    // ── Global options ───────────────────────
    if (root.isMember("daemon") && root["daemon"].isBool())
        g_config.daemon_mode = root["daemon"].asBool();

    if (root.isMember("debug") && root["debug"].isBool())
        g_config.debug = root["debug"].asBool();

    if (root.isMember("control_socket") && root["control_socket"].isString())
        g_config.control_socket = root["control_socket"].asString();

    // ── Devices array ────────────────────────
    if (!root.isMember("devices") || !root["devices"].isArray()) {
        std::cerr << "[config] Missing or invalid 'devices' array.\n";
        return false;
    }

    for (const auto& dev : root["devices"]) {
        DeviceConfig dc;

        if (!dev.isMember("id") || !dev["id"].isString()) {
            std::cerr << "[config] Device entry missing 'id' field – skipping.\n";
            continue;
        }
        dc.id = dev["id"].asString();

        // ── Match criteria ───────────────────
        if (dev.isMember("match") && dev["match"].isObject()) {
            const auto& m = dev["match"];
            if (m.isMember("name")           && m["name"].isString())
                dc.match.name_contains  = m["name"].asString();
            if (m.isMember("vendor_product") && m["vendor_product"].isString())
                dc.match.vendor_product = m["vendor_product"].asString();
            if (m.isMember("path")           && m["path"].isString())
                dc.match.path           = m["path"].asString();
        } else if (dev.isMember("path") && dev["path"].isString()) {
            // Shorthand: bare "path" key on the device object.
            dc.match.path = dev["path"].asString();
        }

        if (dc.match.name_contains.empty()   &&
            dc.match.vendor_product.empty()  &&
            dc.match.path.empty()            &&
            dc.id != "*")
        {
            std::cerr << "[config] Device '" << dc.id
                      << "' has no match criteria – skipping.\n";
            continue;
        }

        if (dev.isMember("grab") && dev["grab"].isBool())
            dc.grab = dev["grab"].asBool();

        // ── Bindings ─────────────────────────
        if (dev.isMember("bindings") && dev["bindings"].isObject()) {
            for (const auto& key_name : dev["bindings"].getMemberNames()) {
                int code = Mapper::name_to_code(key_name);
                if (code < 0) {
                    std::cerr << "[config] Unknown key '" << key_name
                              << "' in device '" << dc.id << "' – skipping.\n";
                    continue;
                }
                dc.bindings[code] = dev["bindings"][key_name].asString();
            }
        }

        g_config.devices.push_back(std::move(dc));
    }

    return true;
}

bool ConfigLoader::validate()
{
    if (g_config.devices.empty()) {
        std::cerr << "[config] No devices configured.\n";
        return false;
    }
    for (const auto& dc : g_config.devices) {
        if (dc.bindings.empty())
            std::cerr << "[config] Warning: device '" << dc.id
                      << "' has no bindings.\n";
    }
    return true;
}

void ConfigLoader::dump()
{
    std::cout
        << "[config] version       : " << VERSION << "\n"
        << "[config] daemon        : " << (g_config.daemon_mode ? "true" : "false") << "\n"
        << "[config] debug         : " << (g_config.debug       ? "true" : "false") << "\n"
        << "[config] control_socket: " << g_config.control_socket << "\n"
        << "[config] devices       : " << g_config.devices.size() << "\n";

    for (const auto& dc : g_config.devices) {
        std::cout
            << "  [" << dc.id << "]\n"
            << "    match.name    : " << dc.match.name_contains  << "\n"
            << "    match.vp      : " << dc.match.vendor_product  << "\n"
            << "    match.path    : " << dc.match.path            << "\n"
            << "    grab          : " << (dc.grab ? "true" : "false") << "\n"
            << "    bindings      : " << dc.bindings.size()       << "\n";
        for (const auto& [code, cmd] : dc.bindings)
            std::cout << "      " << Mapper::code_to_name(code)
                      << " (" << code << ") → " << cmd << "\n";
    }
}

} // namespace pf
