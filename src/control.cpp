#include "control.hpp"
#include "config.hpp"
#include "mapper.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

#include <json/json.h>

// ─────────────────────────────────────────────
//  control.cpp
// ─────────────────────────────────────────────

namespace pf {

// ── Socket helpers ────────────────────────────

static bool send_str(int fd, const std::string& s)
{
    size_t sent = 0;
    while (sent < s.size()) {
        ssize_t n = ::write(fd, s.data() + sent, s.size() - sent);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

// JSON-lines helpers: FastWriter always appends '\n'.
static std::string ok_response(Json::Value payload = Json::objectValue)
{
    payload["ok"] = true;
    Json::FastWriter w;
    return w.write(payload);
}

static std::string err_response(const std::string& msg)
{
    Json::Value v;
    v["ok"]    = false;
    v["error"] = msg;
    Json::FastWriter w;
    return w.write(v);
}

// ── ControlServer::start ─────────────────────

bool ControlServer::start()
{
    ::unlink(g_config.control_socket.c_str()); // remove stale socket

    server_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd_ < 0) {
        std::cerr << "[ctrl] socket(): " << std::strerror(errno) << "\n";
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path,
                 g_config.control_socket.c_str(),
                 sizeof(addr.sun_path) - 1);

    if (::bind(server_fd_,
               reinterpret_cast<struct sockaddr*>(&addr),
               sizeof(addr)) < 0)
    {
        std::cerr << "[ctrl] bind(): " << std::strerror(errno) << "\n";
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (::listen(server_fd_, 8) < 0) {
        std::cerr << "[ctrl] listen(): " << std::strerror(errno) << "\n";
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_ = true;
    thread_  = std::thread(&ControlServer::accept_loop, this);
    std::cout << "[ctrl] Control server: " << g_config.control_socket << "\n";
    return true;
}

void ControlServer::stop()
{
    running_ = false;
    if (server_fd_ >= 0) {
        ::shutdown(server_fd_, SHUT_RDWR);
        ::close(server_fd_);
        server_fd_ = -1;
    }
    if (thread_.joinable())
        thread_.join();
    ::unlink(g_config.control_socket.c_str());
}

// ── accept_loop ──────────────────────────────

void ControlServer::accept_loop()
{
    while (running_) {
        struct pollfd pfd{ server_fd_, POLLIN, 0 };
        if (::poll(&pfd, 1, 200) <= 0) continue;

        int client_fd = ::accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0) continue;

        std::thread([this, client_fd]{
            handle_client(client_fd);
            ::close(client_fd);
        }).detach();
    }
}

// ── handle_client ────────────────────────────

void ControlServer::handle_client(int fd)
{
    // Per-connection receive timeout (avoids hanging on idle clients).
    struct timeval tv{ 30, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const void*>(&tv), sizeof(tv));

    std::string buf;
    char        tmp[2048];

    while (true) {
        // ── Read until '\n' ──────────────────
        while (buf.find('\n') == std::string::npos) {
            ssize_t n = ::read(fd, tmp, sizeof(tmp));
            if (n <= 0) return;
            buf.append(tmp, static_cast<size_t>(n));
        }

        auto nl  = buf.find('\n');
        auto line = buf.substr(0, nl);
        buf.erase(0, nl + 1);
        if (line.empty()) continue;

        // ── Parse JSON command ───────────────
        Json::Value  req;
        Json::Reader reader;
        if (!reader.parse(line, req) || !req.isMember("cmd")) {
            send_str(fd, err_response("Invalid JSON or missing 'cmd'"));
            continue;
        }
        std::string cmd = req["cmd"].asString();

        // ────────────────────────────────────
        //  STATUS
        // ────────────────────────────────────
        if (cmd == "status") {
            auto    info     = dm_.get_info();
            auto    bindings = mapper_.all_bindings();
            Json::Value resp;
            resp["version"]       = VERSION;
            resp["running"]       = g_running.load();
            resp["device_count"]  = static_cast<int>(info.size());
            resp["binding_count"] = static_cast<int>(bindings.size());
            resp["config"]        = g_config.config_path;
            send_str(fd, ok_response(resp));
        }

        // ────────────────────────────────────
        //  DEVICES
        // ────────────────────────────────────
        else if (cmd == "devices") {
            Json::Value arr(Json::arrayValue);
            for (const auto& di : dm_.get_info()) {
                Json::Value d;
                d["id"]     = di.id;
                d["path"]   = di.path;
                d["name"]   = di.hw_name;
                d["active"] = di.active;
                arr.append(d);
            }
            Json::Value resp;
            resp["devices"] = arr;
            send_str(fd, ok_response(resp));
        }

        // ────────────────────────────────────
        //  BINDINGS  [,"device":"<id>"]
        // ────────────────────────────────────
        else if (cmd == "bindings") {
            std::string filter = req.isMember("device")
                                 ? req["device"].asString() : "";
            Json::Value arr(Json::arrayValue);
            for (const auto& b : mapper_.all_bindings()) {
                if (!filter.empty() && b.device_id != filter) continue;
                Json::Value e;
                e["device"]  = b.device_id;
                e["key"]     = b.key_name;
                e["code"]    = b.keycode;
                e["command"] = b.command;
                arr.append(e);
            }
            Json::Value resp;
            resp["bindings"] = arr;
            send_str(fd, ok_response(resp));
        }

        // ────────────────────────────────────
        //  BIND  ,"device","key","command"
        // ────────────────────────────────────
        else if (cmd == "bind") {
            if (!req.isMember("device") ||
                !req.isMember("key")    ||
                !req.isMember("command"))
            {
                send_str(fd, err_response(
                    "bind requires fields: device, key, command"));
                continue;
            }
            bool ok = mapper_.add_binding(req["device"].asString(),
                                          req["key"].asString(),
                                          req["command"].asString());
            send_str(fd, ok ? ok_response()
                            : err_response("Unknown key name"));
        }

        // ────────────────────────────────────
        //  UNBIND  ,"device","key"
        // ────────────────────────────────────
        else if (cmd == "unbind") {
            if (!req.isMember("device") || !req.isMember("key")) {
                send_str(fd, err_response(
                    "unbind requires fields: device, key"));
                continue;
            }
            bool ok = mapper_.remove_binding(req["device"].asString(),
                                             req["key"].asString());
            send_str(fd, ok ? ok_response()
                            : err_response("Binding not found"));
        }

        // ────────────────────────────────────
        //  EVENTS  (blocking stream until disconnect)
        // ────────────────────────────────────
        else if (cmd == "events") {
            // Register an observer that writes JSON event lines.
            // Use a shared atomic flag so the observer can signal us when
            // the client socket becomes unwriteable.
            auto disconnected = std::make_shared<std::atomic<bool>>(false);

            int obs_id = mux_.add_observer(
                [disconnected, fd](const InputEvent& ev,
                                   const std::string& bound_cmd)
                {
                    if (disconnected->load()) return;
                    if (ev.value != EV_KEY_PRESS) return;

                    Json::Value msg;
                    msg["type"]    = "event";
                    msg["device"]  = ev.device_id;
                    msg["path"]    = ev.device_path;
                    msg["key"]     = Mapper::code_to_name(ev.code);
                    msg["code"]    = ev.code;
                    msg["command"] = bound_cmd;
                    Json::FastWriter w;
                    if (!send_str(fd, w.write(msg)))
                        disconnected->store(true);
                });

            // Block until the client disconnects or the daemon stops.
            while (!disconnected->load() && g_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Detect remote close via a zero-length peek.
                char peek;
                ssize_t r = ::recv(fd, &peek, 1, MSG_PEEK | MSG_DONTWAIT);
                if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
                    break;
            }

            mux_.remove_observer(obs_id);
            return; // close connection
        }

        // ────────────────────────────────────
        //  RELOAD
        // ────────────────────────────────────
        else if (cmd == "reload") {
            bool ok = ConfigLoader::load(g_config.config_path);
            if (ok) {
                mapper_.init();
                std::cout << "[ctrl] Config reloaded via control socket.\n";
            }
            send_str(fd, ok ? ok_response()
                            : err_response("Config reload failed – see log"));
        }

        // ────────────────────────────────────
        //  UNKNOWN
        // ────────────────────────────────────
        else {
            send_str(fd, err_response("Unknown command: " + cmd));
        }
    }
}

} // namespace pf
