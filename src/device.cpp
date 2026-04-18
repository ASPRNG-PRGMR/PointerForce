#include "device.hpp"
#include "common.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <cctype>

// ─────────────────────────────────────────────
//  device.cpp
// ─────────────────────────────────────────────

namespace pf {

// ── Move ─────────────────────────────────────

Device::Device(Device&& o) noexcept
    : fd_(o.fd_), dev_(o.dev_), grabbed_(o.grabbed_), path_(std::move(o.path_))
{
    o.fd_      = -1;
    o.dev_     = nullptr;
    o.grabbed_ = false;
}

Device& Device::operator=(Device&& o) noexcept
{
    if (this != &o) {
        close();
        fd_      = o.fd_;
        dev_     = o.dev_;
        grabbed_ = o.grabbed_;
        path_    = std::move(o.path_);
        o.fd_    = -1;
        o.dev_   = nullptr;
        o.grabbed_ = false;
    }
    return *this;
}

// ── open ─────────────────────────────────────

bool Device::open(const std::string& path)
{
    fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        if (g_config.debug)
            std::cerr << "[device] open(" << path << "): "
                      << std::strerror(errno) << "\n";
        return false;
    }

    int rc = libevdev_new_from_fd(fd_, &dev_);
    if (rc < 0) {
        std::cerr << "[device] libevdev_new_from_fd(" << path << "): "
                  << std::strerror(-rc) << "\n";
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    path_ = path;

    if (g_config.debug)
        std::cout << "[device] Probed: " << name() << " (" << path << ")\n";

    return true;
}

// ── grab / ungrab ─────────────────────────────

bool Device::grab()
{
    if (fd_ < 0) return false;
    int rc = libevdev_grab(dev_, LIBEVDEV_GRAB);
    if (rc < 0) {
        std::cerr << "[device] grab(" << path_ << "): "
                  << std::strerror(-rc) << "\n";
        return false;
    }
    grabbed_ = true;
    if (g_config.debug)
        std::cout << "[device] Exclusive grab: " << path_ << "\n";
    return true;
}

void Device::ungrab()
{
    if (fd_ >= 0 && grabbed_) {
        libevdev_grab(dev_, LIBEVDEV_UNGRAB);
        grabbed_ = false;
    }
}

// ── close ────────────────────────────────────

void Device::close()
{
    ungrab();
    if (dev_) { libevdev_free(dev_); dev_ = nullptr; }
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    path_.clear();
}

// ── name ─────────────────────────────────────

const char* Device::name() const
{
    if (!dev_) return "<no device>";
    const char* n = libevdev_get_name(dev_);
    return n ? n : "<unnamed>";
}

// ── vendor_product ────────────────────────────

std::string Device::vendor_product() const
{
    if (path_.empty()) return {};

    auto slash = path_.rfind('/');
    std::string node = (slash == std::string::npos)
                       ? path_ : path_.substr(slash + 1);

    std::string base = "/sys/class/input/" + node + "/device/id/";

    auto read_hex = [&](const std::string& file) -> std::string {
        std::ifstream f(base + file);
        std::string s;
        if (!(f >> s)) return {};
        for (char& c : s) c = std::tolower(static_cast<unsigned char>(c));
        return s;
    };

    std::string v = read_hex("vendor");
    std::string p = read_hex("product");
    if (v.empty() || p.empty()) return {};
    return v + ":" + p;
}

// ── matches ──────────────────────────────────

bool Device::matches(const DeviceMatch& m) const
{
    // Exact path takes priority.
    if (!m.path.empty())
        return path_ == m.path;

    // Vendor:product match.
    if (!m.vendor_product.empty()) {
        std::string vp = vendor_product();
        if (!vp.empty() && vp == m.vendor_product)
            return true;
    }

    // Name substring match.
    if (!m.name_contains.empty()) {
        std::string n = name();
        if (n.find(m.name_contains) != std::string::npos)
            return true;
    }

    return false;
}

} // namespace pf
