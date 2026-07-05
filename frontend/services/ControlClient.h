#pragma once

#include <json/json.h>

#include <atomic>
#include <functional>
#include <string>

// Speaks PointerForce's control-plane protocol directly over the Unix
// socket, per control.cpp/control.hpp:
//
//   - One JSON object per line, newline-terminated, both directions
//   - Request:  {"cmd": "<name>", ...fields}
//   - Response: {"ok": true/false, ...fields} / {"ok": false, "error": "..."}
//   - "events" is the one exception: after the request line, the daemon
//     streams one JSON object per line indefinitely (no "ok" wrapper),
//     until the client disconnects.
//
// This replaces PfctlRunner. Two concrete things that fixes, beyond not
// needing pfctl on PATH:
//   1. No more scraping pfctl's human-readable text output - we get the
//      exact structured fields control.cpp put there, so a wording change
//      in the daemon's pretty-printer can't silently break this bridge.
//   2. pfctl.cpp hardcodes /tmp/pointerforce.sock and ignores
//      control_socket entirely (see DEPLOYMENT.md) - resolveSocketPath()
//      here reads control_socket from the same config file ConfigStore
//      already resolves, so the two actually agree.
class ControlClient
{
public:
    // Socket path used if the config file doesn't specify control_socket,
    // or can't be read at all. Matches pfctl.cpp's compiled-in default and
    // the example config, so behavior is unchanged for anyone not using a
    // custom socket path.
    static const std::string kDefaultSocketPath;

    // Reads control_socket out of the config file at ConfigStore::resolvePath().
    // Falls back to kDefaultSocketPath if the field is missing or the file
    // can't be read - deliberately never fails outright, since not being
    // able to determine the socket path shouldn't be fatal to callers that
    // are about to get a clear "connection refused"-style error anyway.
    static std::string resolveSocketPath();

    // Result of a single request/response command (anything but "events").
    struct Result
    {
        bool transportOk = false;   // connected, wrote request, read a response line
        std::string transportError; // set when transportOk is false
        Json::Value response;       // parsed response object when transportOk; check response["ok"]
    };

    // Opens a fresh connection, sends `request` as one JSON-lines command,
    // reads exactly one JSON-lines response line, then closes. Matches
    // pfctl.cpp's own connect-per-command pattern - no persistent
    // connection to manage or get out of sync.
    static Result send(const Json::Value &request);

    // Opens its own connection, sends {"cmd":"events"}, then invokes
    // onEvent for each event object the daemon streams back, until
    // keepRunning becomes false or the daemon closes the connection.
    // Blocking - intended to run on a dedicated thread, same calling
    // convention as PfctlRunner::streamEvents had.
    static void streamEvents(const std::atomic<bool> &keepRunning,
                              const std::function<void(const Json::Value &)> &onEvent);
};
