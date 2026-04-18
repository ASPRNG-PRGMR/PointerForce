#pragma once

#include "common.hpp"
#include "device.hpp"
#include "eventqueue.hpp"

#include <list>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>

// ─────────────────────────────────────────────
//  devicemanager.hpp : multi-device lifecycle
//
//  - Scans /dev/input/event* and matches each node against g_config.devices.
//  - Owns Device objects and per-device reader threads.
//  - Reader threads push EV_KEY events into the shared EventQueue.
// ─────────────────────────────────────────────

namespace pf {

class DeviceManager {
public:
    explicit DeviceManager(EventQueue& queue) : queue_(queue) {}
    ~DeviceManager() { stop(); }

    // Scan /dev/input/event*, open every device that matches a DeviceConfig.
    // Returns the number of successfully matched+opened devices.
    int open_all();

    // Spawn one reader thread per open device.
    void start();

    // Signal all reader threads and join them, then close devices.
    void stop();

    // ── Status info (used by ControlServer) ──
    struct DeviceInfo {
        std::string id;
        std::string path;
        std::string hw_name;
        bool        active;
    };
    std::vector<DeviceInfo> get_info() const;

private:
    struct Entry {
        DeviceConfig       cfg;
        Device             device;
        std::thread        thread;
        std::atomic<bool>  active{false};

        Entry() = default;
        Entry(const Entry&)            = delete;
        Entry& operator=(const Entry&) = delete;
    };

    EventQueue&        queue_;
    std::list<Entry>   entries_;
    mutable std::mutex mu_;

    static void reader_fn(Entry& e, EventQueue& q);
};

} // namespace pf
