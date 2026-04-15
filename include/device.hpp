#pragma once

#include <string>
#include <libevdev/libevdev.h>

// ─────────────────────────────────────────────
//  device.hpp : open input device via libevdev
// ─────────────────────────────────────────────

namespace pf {

class Device {
public:
    Device() = default;
    ~Device();

    // Open device at path and initialise libevdev.
    // Returns true on success.
    bool open(const std::string& path);

    // Optionally grab the device so no other process sees events.
    bool grab();
    void ungrab();

    // Release fd and libevdev context.
    void close();

    // True if device is open and ready.
    bool is_open() const { return fd_ >= 0; }

    // Underlying libevdev handle (used by EventLoop).
    struct libevdev* dev() const { return dev_; }

    // Human-readable name from the device.
    const char* name() const;

private:
    int           fd_  = -1;
    struct libevdev* dev_ = nullptr;
    bool          grabbed_ = false;
};

} // namespace pf
