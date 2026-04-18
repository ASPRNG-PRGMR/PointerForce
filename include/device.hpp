#pragma once

#include "common.hpp"
#include <string>
#include <libevdev/libevdev.h>

// ─────────────────────────────────────────────
//  device.hpp : open / grab / close one input device via libevdev
// ─────────────────────────────────────────────

namespace pf {

class Device {
public:
    Device()  = default;
    ~Device() { close(); }

    // Non-copyable; moveable so DeviceManager can own entries in a vector.
    Device(const Device&)            = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&& o) noexcept;
    Device& operator=(Device&& o) noexcept;

    // Open device at path and initialise libevdev.  Returns true on success.
    bool open(const std::string& path);

    // Exclusively grab the device (no other process sees events).
    bool grab();
    void ungrab();

    // Release fd and libevdev context.
    void close();

    bool             is_open() const { return fd_ >= 0; }
    struct libevdev* dev()     const { return dev_;     }
    std::string      path()    const { return path_;    }
    const char*      name()    const;

    // True if this opened device satisfies the given match criteria.
    bool matches(const DeviceMatch& m) const;

    // Read "VVVV:PPPP" from sysfs for this device.  Empty if unavailable.
    std::string vendor_product() const;

private:
    int              fd_      = -1;
    struct libevdev* dev_     = nullptr;
    bool             grabbed_ = false;
    std::string      path_;
};

} // namespace pf
