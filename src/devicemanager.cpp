#include "devicemanager.hpp"
#include "mapper.hpp"

#include <filesystem>
#include <algorithm>
#include <chrono>
#include <thread>
#include <list>
#include <fstream>
#include <iostream>
#include <cerrno>
#include <cstring>
#include <cctype>

#include <linux/input.h>
#include <libevdev/libevdev.h>

// ─────────────────────────────────────────────
//  devicemanager.cpp
// ─────────────────────────────────────────────

namespace pf {

// ── Helpers ──────────────────────────────────

static uint64_t now_us()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

// Read a single-line sysfs file, return its content trimmed.
// Returns "" on any failure.
static std::string sysfs_read(const std::string& path)
{
    std::ifstream f(path);
    std::string s;
    if (!std::getline(f, s)) return {};
    // Trim trailing whitespace / \r
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' ||
                           s.back() == ' '  || s.back() == '\t'))
        s.pop_back();
    return s;
}

// Probe device identity from sysfs without opening the fd.
// Returns the device name and "VVVV:PPPP" vendor:product string.
// Safe to call on any event node without touching libevdev.
struct SysfsInfo {
    std::string name;           // e.g. "Logitech G502 HERO"
    std::string vendor_product; // e.g. "046d:c08b"
};

static SysfsInfo probe_sysfs(const std::string& event_path)
{
    // Derive sysfs base from /dev/input/eventN → /sys/class/input/eventN/device/
    auto slash = event_path.rfind('/');
    std::string node = (slash == std::string::npos)
                       ? event_path : event_path.substr(slash + 1);
    std::string base = "/sys/class/input/" + node + "/device/";

    SysfsInfo info;
    info.name = sysfs_read(base + "name");

    auto to_lower = [](std::string s) {
        for (char& c : s) c = std::tolower(static_cast<unsigned char>(c));
        return s;
    };

    std::string vendor  = to_lower(sysfs_read(base + "id/vendor"));
    std::string product = to_lower(sysfs_read(base + "id/product"));
    if (!vendor.empty() && !product.empty())
        info.vendor_product = vendor + ":" + product;

    return info;
}

// Check if a sysfs identity matches a DeviceMatch without opening the device.
static bool sysfs_matches(const SysfsInfo& info,
                           const std::string& event_path,
                           const DeviceMatch& m)
{
    if (!m.path.empty())
        return event_path == m.path;

    if (!m.vendor_product.empty() && !info.vendor_product.empty())
        if (info.vendor_product == m.vendor_product)
            return true;

    if (!m.name_contains.empty() && !info.name.empty())
        if (info.name.find(m.name_contains) != std::string::npos)
            return true;

    return false;
}

// ── open_all ─────────────────────────────────

int DeviceManager::open_all()
{
    namespace fs = std::filesystem;

    // ── Collect event node paths ──────────────
    std::vector<std::string> candidates;
    try {
        std::error_code ec;
        for (const auto& entry :
             fs::directory_iterator("/dev/input", ec))
        {
            if (ec) { ec.clear(); continue; }
            std::string fname = entry.path().filename().string();
            if (fname.rfind("event", 0) == 0)
                candidates.push_back(entry.path().string());
        }
    } catch (const std::exception& ex) {
        std::cerr << "[devicemgr] Failed to scan /dev/input: "
                  << ex.what() << "\n" << std::flush;
        return 0;
    }

    std::sort(candidates.begin(), candidates.end());

    if (g_config.debug)
        std::cout << "[devicemgr] Found " << candidates.size()
                  << " event node(s) in /dev/input.\n" << std::flush;

    std::lock_guard<std::mutex> lock(mu_);
    entries_.clear();

    std::vector<bool> matched(g_config.devices.size(), false);

    for (const auto& cand_path : candidates) {
        if (g_config.debug)
            std::cerr << "[devicemgr] Scanning: " << cand_path << "\n" << std::flush;
        // ── Step 1: read identity from sysfs (no fd open, no libevdev) ──
        SysfsInfo sysfs = probe_sysfs(cand_path);

        if (g_config.debug)
            std::cout << "[devicemgr] Probe sysfs: " << cand_path
                      << "  name='" << sysfs.name
                      << "'  vp='" << sysfs.vendor_product << "'\n"
                      << std::flush;

        // ── Step 2: find a matching DeviceConfig ──────────────────────
        int match_idx = -1;
        for (size_t i = 0; i < g_config.devices.size(); ++i) {
            if (matched[i]) continue;
            if (sysfs_matches(sysfs, cand_path, g_config.devices[i].match)) {
                match_idx = static_cast<int>(i);
                break;
            }
        }
        if (match_idx < 0) continue;

        // ── Step 3: only now open with libevdev ───────────────────────
        entries_.emplace_back();
        Entry& e = entries_.back();
        e.cfg = g_config.devices[static_cast<size_t>(match_idx)];

        if (!e.device.open(cand_path)) {
            std::cerr << "[devicemgr] Failed to open matched device '"
                      << e.cfg.id << "' at " << cand_path << "\n"
                      << std::flush;
            entries_.pop_back();
            continue;
        }

        matched[static_cast<size_t>(match_idx)] = true;

        std::cout << "[devicemgr] Matched '" << e.cfg.id
                  << "' -> " << cand_path
                  << " (" << e.device.name() << ")\n" << std::flush;
    }

    int n = static_cast<int>(entries_.size());
    if (n == 0)
        std::cerr << "[devicemgr] Warning: no configured devices found "
                     "on /dev/input. Check config match criteria and "
                     "'input' group membership.\n" << std::flush;
    return n;
}

// ── start ────────────────────────────────────

void DeviceManager::start()
{
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& e : entries_) {
        if (!e.device.is_open()) continue;
        if (e.cfg.grab) {
            if (!e.device.grab())
                std::cerr << "[devicemgr] Warning: grab failed for '"
                          << e.cfg.id << "'\n";
        }
        e.active = true;
        e.thread = std::thread(reader_fn, std::ref(e), std::ref(queue_));
    }
}

// ── stop ─────────────────────────────────────

void DeviceManager::stop()
{
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& e : entries_)
            e.active = false;
    }
    for (auto& e : entries_) {
        if (e.thread.joinable())
            e.thread.join();
    }
    for (auto& e : entries_)
        e.device.close();

    std::lock_guard<std::mutex> lock(mu_);
    entries_.clear();
}

// ── get_info ─────────────────────────────────

std::vector<DeviceManager::DeviceInfo> DeviceManager::get_info() const
{
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<DeviceInfo> info;
    info.reserve(entries_.size());
    for (const auto& e : entries_) {
        info.push_back({
            e.cfg.id,
            e.device.path(),
            std::string(e.device.name()),
            e.active.load()
        });
    }
    return info;
}

// ── reader_fn (per-device thread) ────────────

void DeviceManager::reader_fn(Entry& e, EventQueue& q)
{
    struct input_event raw{};
    int rc;

    while (e.active && g_running) {
        rc = libevdev_next_event(
                 e.device.dev(),
                 LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING,
                 &raw);

        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (raw.type != EV_KEY) continue;

            InputEvent ev;
            ev.device_id   = e.cfg.id;
            ev.device_path = e.device.path();
            ev.code        = raw.code;
            ev.value       = raw.value;
            ev.timestamp   = now_us();
            q.push(std::move(ev));
            continue;
        }

        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            while ((rc = libevdev_next_event(
                        e.device.dev(),
                        LIBEVDEV_READ_FLAG_SYNC,
                        &raw)) == LIBEVDEV_READ_STATUS_SYNC) {}
            continue;
        }

        if (rc == -EAGAIN) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        std::cerr << "[devicemgr] Device '" << e.cfg.id
                  << "' disconnected: " << std::strerror(-rc) << "\n"
                  << std::flush;
        e.active = false;
        break;
    }

    if (g_config.debug)
        std::cout << "[devicemgr] Reader thread for '" << e.cfg.id
                  << "' exiting.\n" << std::flush;
}

} // namespace pf
