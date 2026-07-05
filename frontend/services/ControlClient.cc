#include "ControlClient.h"
#include "ConfigStore.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

const std::string ControlClient::kDefaultSocketPath = "/tmp/pointerforce.sock";

namespace
{

// Opens a fresh AF_UNIX SOCK_STREAM connection to `path`. Returns -1 on
// failure (caller checks errno via strerror for the error message).
int connectSocket(const std::string &path)
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        ::close(fd);
        return -1;
    }

    // Guard against a wedged daemon leaving us blocked forever - the
    // daemon side already applies a 30s SO_RCVTIMEO per control.cpp;
    // matching a client-side timeout means a bad connection fails
    // cleanly on our end too, rather than tying up a Drogon IO thread.
    struct timeval tv{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    return fd;
}

bool sendAll(int fd, const std::string &s)
{
    size_t sent = 0;
    while (sent < s.size())
    {
        ssize_t n = ::write(fd, s.data() + sent, s.size() - sent);
        if (n <= 0)
            return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

// Reads exactly one newline-terminated line, blocking (subject to the
// socket's SO_RCVTIMEO). Returns "" on EOF/error/timeout with no data.
std::string readLine(int fd)
{
    std::string buf;
    char c;
    while (true)
    {
        ssize_t n = ::read(fd, &c, 1);
        if (n <= 0)
            break;
        if (c == '\n')
            break;
        buf += c;
    }
    return buf;
}

}  // namespace

std::string ControlClient::resolveSocketPath()
{
    Json::Value cfg;
    std::string err;
    if (ConfigStore::read(ConfigStore::resolvePath(), cfg, err) && cfg.isMember("control_socket") &&
        cfg["control_socket"].isString() && !cfg["control_socket"].asString().empty())
    {
        return cfg["control_socket"].asString();
    }
    return kDefaultSocketPath;
}

ControlClient::Result ControlClient::send(const Json::Value &request)
{
    Result result;

    std::string socketPath = resolveSocketPath();
    int fd = connectSocket(socketPath);
    if (fd < 0)
    {
        result.transportError = "cannot connect to '" + socketPath + "': " + std::strerror(errno) +
                                 " (is the pointerforce daemon running, and is control_socket "
                                 "in config set to this path?)";
        return result;
    }

    Json::FastWriter writer;
    std::string line = writer.write(request);  // FastWriter appends '\n'

    if (!sendAll(fd, line))
    {
        result.transportError = "write to control socket failed: " + std::string(std::strerror(errno));
        ::close(fd);
        return result;
    }

    std::string respLine = readLine(fd);
    ::close(fd);

    if (respLine.empty())
    {
        result.transportError = "empty or timed-out response from control socket";
        return result;
    }

    Json::Reader reader;
    if (!reader.parse(respLine, result.response))
    {
        result.transportError = "invalid JSON in control socket response: " + respLine;
        return result;
    }

    result.transportOk = true;
    return result;
}

void ControlClient::streamEvents(const std::atomic<bool> &keepRunning,
                                  const std::function<void(const Json::Value &)> &onEvent)
{
    std::string socketPath = resolveSocketPath();
    int fd = connectSocket(socketPath);
    if (fd < 0)
        return;

    Json::Value req;
    req["cmd"] = "events";
    Json::FastWriter writer;
    if (!sendAll(fd, writer.write(req)))
    {
        ::close(fd);
        return;
    }

    std::string leftover;
    char buf[1024];

    while (keepRunning.load())
    {
        struct pollfd pfd
        {
            fd, POLLIN, 0
        };
        // Short timeout so we periodically re-check keepRunning even when
        // the daemon has nothing new to report.
        int ret = poll(&pfd, 1, 200);
        if (ret > 0 && (pfd.revents & POLLIN))
        {
            ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n <= 0)
                break;  // daemon closed the connection
            leftover.append(buf, static_cast<size_t>(n));
            size_t pos;
            while ((pos = leftover.find('\n')) != std::string::npos)
            {
                std::string line = leftover.substr(0, pos);
                leftover.erase(0, pos + 1);
                if (line.empty())
                    continue;
                Json::Value event;
                Json::Reader reader;
                if (reader.parse(line, event))
                    onEvent(event);
            }
        }
        else if (ret > 0 && (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)))
        {
            // See the equivalent check in the old PfctlRunner: without
            // this, poll() returns immediately (ignoring the timeout) on
            // every call once POLLHUP is set with no POLLIN alongside it,
            // spinning the thread instead of exiting.
            break;
        }
        else if (ret < 0)
        {
            break;
        }
    }

    ::close(fd);
}
