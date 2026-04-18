#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <json/json.h>

// ─────────────────────────────────────────────
//  pfctl.cpp – PointerForce control client
//
//  Usage:
//    pfctl status
//    pfctl devices
//    pfctl bindings [<device_id>]
//    pfctl bind   <device_id> <KEY_NAME> <command>
//    pfctl unbind <device_id> <KEY_NAME>
//    pfctl events        (live stream, Ctrl-C to quit)
//    pfctl reload
// ─────────────────────────────────────────────

static const char* SOCKET_PATH = "/tmp/pointerforce.sock";

static void usage()
{
    std::cout <<
        "pfctl – PointerForce control client\n"
        "\n"
        "Usage:\n"
        "  pfctl status\n"
        "  pfctl devices\n"
        "  pfctl bindings [<device_id>]\n"
        "  pfctl bind   <device_id> <KEY_NAME> <command>\n"
        "  pfctl unbind <device_id> <KEY_NAME>\n"
        "  pfctl events\n"
        "  pfctl reload\n"
        "\n"
        "Socket: " << SOCKET_PATH << "\n";
}

// ── Socket helpers ────────────────────────────

static int connect_socket()
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (::connect(fd,
                  reinterpret_cast<struct sockaddr*>(&addr),
                  sizeof(addr)) < 0)
    {
        std::cerr << "pfctl: cannot connect to " << SOCKET_PATH
                  << ": " << std::strerror(errno)
                  << "\n(Is pointerforce running?)\n";
        ::close(fd);
        return -1;
    }
    return fd;
}

static bool send_all(int fd, const std::string& s)
{
    size_t sent = 0;
    while (sent < s.size()) {
        ssize_t n = ::write(fd, s.data() + sent, s.size() - sent);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

// Read one newline-terminated line from fd.
static std::string read_line(int fd)
{
    std::string buf;
    char c;
    while (true) {
        ssize_t n = ::read(fd, &c, 1);
        if (n <= 0) break;
        if (c == '\n') break;
        buf += c;
    }
    return buf;
}

// ── Pretty-printers ──────────────────────────

static void print_status(const Json::Value& v)
{
    std::cout
        << "Version    : " << v["version"].asString()  << "\n"
        << "Running    : " << (v["running"].asBool() ? "yes" : "no") << "\n"
        << "Devices    : " << v["device_count"].asInt()  << "\n"
        << "Bindings   : " << v["binding_count"].asInt() << "\n"
        << "Config     : " << v["config"].asString()  << "\n";
}

static void print_devices(const Json::Value& v)
{
    for (const auto& d : v["devices"]) {
        std::cout
            << "  [" << d["id"].asString() << "]\n"
            << "    path   : " << d["path"].asString()   << "\n"
            << "    name   : " << d["name"].asString()   << "\n"
            << "    active : " << (d["active"].asBool() ? "yes" : "no") << "\n";
    }
}

static void print_bindings(const Json::Value& v)
{
    if (v["bindings"].empty()) {
        std::cout << "(no bindings)\n";
        return;
    }
    for (const auto& b : v["bindings"]) {
        std::cout
            << "  [" << b["device"].asString() << "] "
            << b["key"].asString()
            << "  →  " << b["command"].asString() << "\n";
    }
}

// ── main ─────────────────────────────────────

int main(int argc, char** argv)
{
    if (argc < 2) { usage(); return 0; }

    std::string cmd = argv[1];

    // ── Build request JSON ────────────────────
    Json::Value req(Json::objectValue);
    req["cmd"] = cmd;

    if (cmd == "bindings" && argc >= 3)
        req["device"] = argv[2];

    if (cmd == "bind") {
        if (argc < 5) {
            std::cerr << "pfctl bind <device> <KEY> <command>\n";
            return 1;
        }
        req["device"]  = argv[2];
        req["key"]     = argv[3];
        // Remaining args joined as command (allows spaces without quoting)
        std::string full_cmd = argv[4];
        for (int i = 5; i < argc; ++i) { full_cmd += ' '; full_cmd += argv[i]; }
        req["command"] = full_cmd;
    }

    if (cmd == "unbind") {
        if (argc < 4) {
            std::cerr << "pfctl unbind <device> <KEY>\n";
            return 1;
        }
        req["device"] = argv[2];
        req["key"]    = argv[3];
    }

    Json::FastWriter writer;
    std::string payload = writer.write(req); // includes trailing '\n'

    // ── Connect & send ────────────────────────
    int fd = connect_socket();
    if (fd < 0) return 1;

    if (!send_all(fd, payload)) {
        std::cerr << "pfctl: write failed\n";
        ::close(fd);
        return 1;
    }

    // ── events: print stream until Ctrl-C ─────
    if (cmd == "events") {
        std::cout << "Listening for events (Ctrl-C to quit)...\n";
        while (true) {
            std::string line = read_line(fd);
            if (line.empty()) break;
            Json::Value msg;
            Json::Reader reader;
            if (reader.parse(line, msg)) {
                std::cout
                    << "[" << msg["device"].asString() << "] "
                    << msg["key"].asString();
                if (!msg["command"].asString().empty())
                    std::cout << "  →  " << msg["command"].asString();
                std::cout << "\n";
            } else {
                std::cout << line << "\n";
            }
        }
        ::close(fd);
        return 0;
    }

    // ── All other commands: read one response ─
    std::string resp_line = read_line(fd);
    ::close(fd);

    if (resp_line.empty()) {
        std::cerr << "pfctl: empty response\n";
        return 1;
    }

    Json::Value resp;
    Json::Reader reader;
    if (!reader.parse(resp_line, resp)) {
        std::cerr << "pfctl: invalid response JSON: " << resp_line << "\n";
        return 1;
    }

    if (!resp["ok"].asBool()) {
        std::cerr << "Error: " << resp["error"].asString() << "\n";
        return 1;
    }

    if      (cmd == "status")   print_status(resp);
    else if (cmd == "devices")  print_devices(resp);
    else if (cmd == "bindings") print_bindings(resp);
    else    std::cout << "OK\n";

    return 0;
}
