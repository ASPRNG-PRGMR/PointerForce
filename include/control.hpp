#pragma once

#include "common.hpp"
#include "mapper.hpp"
#include "devicemanager.hpp"
#include "multiplexer.hpp"

#include <thread>
#include <atomic>

// ─────────────────────────────────────────────
//  control.hpp : Phase 2.5 control-plane server
//
//  Listens on a Unix-domain socket (g_config.control_socket).
//  Each client connection is handled in a detached thread.
//
//  Protocol: JSON-lines (one JSON object per line, newline-terminated).
//
//  Commands  →  {"cmd":"<name>", ...}
//  Responses ←  {"ok":true/false, ...}
//
//  Supported commands:
//    status                          system overview
//    devices                         list active devices
//    bindings [,"device":"<id>"]     list bindings, optionally filtered
//    bind    ,"device","key","command"   add / replace a binding at runtime
//    unbind  ,"device","key"             remove a binding at runtime
//    events                          subscribe to live event stream (blocking)
//    reload                          hot-reload config from disk
// ─────────────────────────────────────────────

namespace pf {

class ControlServer {
public:
    ControlServer(Mapper& mapper, DeviceManager& dm, EventMultiplexer& mux)
        : mapper_(mapper), dm_(dm), mux_(mux) {}
    ~ControlServer() { stop(); }

    // Create, bind and listen on the control socket.
    // Returns false if the socket cannot be set up.
    bool start();

    // Signal the accept loop and join.
    void stop();

private:
    Mapper&           mapper_;
    DeviceManager&    dm_;
    EventMultiplexer& mux_;

    int               server_fd_ = -1;
    std::thread       thread_;
    std::atomic<bool> running_{false};

    void accept_loop();
    void handle_client(int fd);
};

} // namespace pf
