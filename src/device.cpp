#include "device.hpp"
#include "common.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <cerrno>
#include <cstring>
#include <iostream>

// ─────────────────────────────────────────────
//  device.cpp
// ─────────────────────────────────────────────

namespace pf {

Device::~Device()
{
    close();
}

bool Device::open(const std::string& path)
{
    fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        std::cerr << "[device] open(" << path << ") failed: "
                  << std::strerror(errno) << "\n";
        return false;
    }

    int rc = libevdev_new_from_fd(fd_, &dev_);
    if (rc < 0) {
        std::cerr << "[device] libevdev_new_from_fd failed: "
                  << std::strerror(-rc) << "\n";
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    if (g_config.debug) {
        std::cout << "[device] Opened: " << name()
                  << " (" << path << ")\n";
    }
    return true;
}

bool Device::grab()
{
    if (fd_ < 0) return false;
    int rc = libevdev_grab(dev_, LIBEVDEV_GRAB);
    if (rc < 0) {
        std::cerr << "[device] grab failed: " << std::strerror(-rc) << "\n";
        return false;
    }
    grabbed_ = true;
    if (g_config.debug)
        std::cout << "[device] Exclusive grab acquired.\n";
    return true;
}

void Device::ungrab()
{
    if (fd_ >= 0 && grabbed_) {
        libevdev_grab(dev_, LIBEVDEV_UNGRAB);
        grabbed_ = false;
    }
}

void Device::close()
{
    ungrab();
    if (dev_) {
        libevdev_free(dev_);
        dev_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

const char* Device::name() const
{
    if (!dev_) return "<no device>";
    return libevdev_get_name(dev_);
}

} // namespace pf
