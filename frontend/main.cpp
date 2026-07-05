// PointerForce Web UI - bridge server
//
// Serves the static web UI and a small JSON API that talks directly to
// PointerForce's control-plane Unix socket using its JSON-lines protocol
// (see control.cpp/control.hpp) via ControlClient. No dependency on pfctl
// being on PATH, and no scraping of human-readable text output - every
// field here comes straight from the daemon's own JSON.

#include <drogon/drogon.h>
#include <json/json.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>

#include "services/ConfigStore.h"
#include "services/ControlClient.h"

using namespace drogon;

namespace
{

HttpResponsePtr jsonOk(const Json::Value &v)
{
    return HttpResponse::newHttpJsonResponse(v);
}

HttpResponsePtr jsonErr(const std::string &msg, HttpStatusCode code = k500InternalServerError)
{
    Json::Value j;
    j["error"] = msg;
    auto resp = HttpResponse::newHttpJsonResponse(j);
    resp->setStatusCode(code);
    return resp;
}

// Sends `request` over the control socket and hands back a ready-to-use
// (transportOk, daemonOk) pair, folding ControlClient's transport-layer
// failures and the daemon's own {"ok": false, "error": ...} responses into
// a single error path - every call site below wants the same "either way,
// bail with a clear message" handling, so it lives here once.
struct CommandOutcome
{
    bool ok = false;
    Json::Value response;  // valid when ok is true
    std::string error;     // set when ok is false
};

CommandOutcome runCommand(const Json::Value &request)
{
    CommandOutcome outcome;
    ControlClient::Result r = ControlClient::send(request);
    if (!r.transportOk)
    {
        outcome.error = r.transportError;
        return outcome;
    }
    if (!r.response.get("ok", false).asBool())
    {
        outcome.error = r.response.get("error", "unknown error").asString();
        return outcome;
    }
    outcome.ok = true;
    outcome.response = r.response;
    return outcome;
}

// If the running daemon reports loading a different config file than the
// one this editor just read/wrote, attaches a "warning" field explaining
// the mismatch. See ConfigStore::daemonPathWarning() for why this is
// surfaced instead of silently trusted either way.
void attachConfigWarning(Json::Value &j)
{
    std::string warning = ConfigStore::daemonPathWarning();
    if (!warning.empty())
        j["warning"] = warning;
}

// Formats one event object the same way pfctl.cpp used to print it -
// "[device] KEY  ->  command" (arrow omitted if there's no command) - so
// the frontend's existing SSE line parser keeps working unchanged even
// though the transport underneath switched from a pfctl subprocess to a
// direct socket connection.
std::string formatEventLine(const Json::Value &event)
{
    std::string line = "[" + event.get("device", "").asString() + "] " + event.get("key", "").asString();
    std::string command = event.get("command", "").asString();
    if (!command.empty())
        line += "  \xe2\x86\x92  " + command;  // UTF-8 for "->"
    return line;
}

// Backing state for one open /api/events SSE connection. The background
// thread holds a live control-socket connection via ControlClient and
// pushes formatted lines into a queue; Drogon pulls from that queue via
// the stream callback below. When Drogon drops the last reference to the
// callback (stream ended or client disconnected), this destructor stops
// the thread and closes the socket - no manual disconnect handler needed.
struct EventStreamState
{
    std::mutex mtx;
    std::condition_variable cv;
    std::deque<std::string> queue;
    std::atomic<bool> running{true};
    std::thread worker;

    EventStreamState()
    {
        worker = std::thread([this]() {
            ControlClient::streamEvents(running, [this](const Json::Value &event) {
                std::lock_guard<std::mutex> lock(mtx);
                // Cap queue growth if nobody is reading fast enough.
                if (queue.size() > 500)
                    queue.pop_front();
                queue.push_back(formatEventLine(event));
                cv.notify_one();
            });
        });
    }

    ~EventStreamState()
    {
        running = false;
        cv.notify_all();
        if (worker.joinable())
            worker.join();
    }
};

}  // namespace

int main()
{
    // Multiple IO loops so a slow-polling SSE connection can't stall the
    // rest of the API.
    app().setThreadNum(4);
    app().addListener("0.0.0.0", 8081);
    app().setDocumentRoot("../webui");

    app().registerHandler(
        "/api/status",
        [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&callback) {
            Json::Value req;
            req["cmd"] = "status";
            auto outcome = runCommand(req);
            if (!outcome.ok)
            {
                callback(jsonErr("status failed: " + outcome.error));
                return;
            }
            const Json::Value &r = outcome.response;
            Json::Value j;
            j["version"] = r.get("version", "").asString();
            j["running"] = r.get("running", false).asBool() ? "yes" : "no";
            j["devices"] = std::to_string(r.get("device_count", 0).asInt());
            j["bindings"] = std::to_string(r.get("binding_count", 0).asInt());
            j["config"] = r.get("config", "").asString();
            callback(jsonOk(j));
        },
        {Get});

    app().registerHandler(
        "/api/devices",
        [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&callback) {
            Json::Value req;
            req["cmd"] = "devices";
            auto outcome = runCommand(req);
            if (!outcome.ok)
            {
                callback(jsonErr("devices failed: " + outcome.error));
                return;
            }
            Json::Value arr(Json::arrayValue);
            for (const auto &d : outcome.response["devices"])
            {
                Json::Value out;
                out["id"] = d.get("id", "").asString();
                out["path"] = d.get("path", "").asString();
                out["name"] = d.get("name", "").asString();
                out["active"] = d.get("active", false).asBool() ? "yes" : "no";
                arr.append(out);
            }
            callback(jsonOk(arr));
        },
        {Get});

    app().registerHandler(
        "/api/bindings",
        [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&callback) {
            Json::Value req;
            req["cmd"] = "bindings";
            auto outcome = runCommand(req);
            if (!outcome.ok)
            {
                callback(jsonErr("bindings failed: " + outcome.error));
                return;
            }
            Json::Value arr(Json::arrayValue);
            for (const auto &b : outcome.response["bindings"])
            {
                Json::Value out;
                out["device"] = b.get("device", "").asString();
                out["key"] = b.get("key", "").asString();
                out["command"] = b.get("command", "").asString();
                arr.append(out);
            }
            callback(jsonOk(arr));
        },
        {Get});

    app().registerHandler(
        "/api/bind",
        [](const HttpRequestPtr &httpReq, std::function<void(const HttpResponsePtr &)> &&callback) {
            auto json = httpReq->getJsonObject();
            if (!json || !json->isMember("device") || !json->isMember("key") ||
                !json->isMember("command"))
            {
                callback(jsonErr("request body must include device, key, command", k400BadRequest));
                return;
            }
            std::string device = (*json)["device"].asString();
            std::string key = (*json)["key"].asString();
            std::string command = (*json)["command"].asString();
            if (device.empty() || key.empty() || command.empty())
            {
                callback(jsonErr("device, key, and command must be non-empty", k400BadRequest));
                return;
            }

            Json::Value req;
            req["cmd"] = "bind";
            req["device"] = device;
            req["key"] = key;
            req["command"] = command;
            auto outcome = runCommand(req);
            if (!outcome.ok)
            {
                callback(jsonErr("bind failed: " + outcome.error));
                return;
            }
            Json::Value j;
            j["ok"] = true;
            j["output"] = "bound " + device + " " + key;
            callback(jsonOk(j));
        },
        {Post});

    app().registerHandler(
        "/api/unbind",
        [](const HttpRequestPtr &httpReq, std::function<void(const HttpResponsePtr &)> &&callback) {
            auto json = httpReq->getJsonObject();
            if (!json || !json->isMember("device") || !json->isMember("key"))
            {
                callback(jsonErr("request body must include device, key", k400BadRequest));
                return;
            }
            std::string device = (*json)["device"].asString();
            std::string key = (*json)["key"].asString();
            if (device.empty() || key.empty())
            {
                callback(jsonErr("device and key must be non-empty", k400BadRequest));
                return;
            }

            Json::Value req;
            req["cmd"] = "unbind";
            req["device"] = device;
            req["key"] = key;
            auto outcome = runCommand(req);
            if (!outcome.ok)
            {
                callback(jsonErr("unbind failed: " + outcome.error));
                return;
            }
            Json::Value j;
            j["ok"] = true;
            j["output"] = "unbound " + device + " " + key;
            callback(jsonOk(j));
        },
        {Post});

    app().registerHandler(
        "/api/reload",
        [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&callback) {
            Json::Value req;
            req["cmd"] = "reload";
            auto outcome = runCommand(req);
            if (!outcome.ok)
            {
                callback(jsonErr("reload failed: " + outcome.error));
                return;
            }
            Json::Value j;
            j["ok"] = true;
            j["output"] = "config reloaded";
            callback(jsonOk(j));
        },
        {Post});

    // ---- config file endpoints -------------------------------------
    //
    // Everything above talks to the *running* daemon over the control
    // socket. These instead read/write pointerforce.json directly, since
    // there's no control-socket command for structural edits (adding/
    // removing a device block) - only for live bindings. Nothing here
    // reloads the daemon; the frontend calls POST /api/reload explicitly
    // afterwards, so a save is always a visible two-step "write, then
    // apply" rather than a silent hot-reload.

    app().registerHandler(
        "/api/config",
        [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&callback) {
            std::string path = ConfigStore::resolvePath();
            Json::Value cfg;
            std::string err;
            if (!ConfigStore::read(path, cfg, err))
            {
                callback(jsonErr("failed to read config: " + err));
                return;
            }
            Json::Value j;
            j["path"] = path;
            j["config"] = cfg;
            attachConfigWarning(j);
            callback(jsonOk(j));
        },
        {Get});

    app().registerHandler(
        "/api/config",
        [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
            auto json = req->getJsonObject();
            if (!json)
            {
                callback(jsonErr("request body must be valid JSON", k400BadRequest));
                return;
            }
            // Accept either the raw config object, or {"config": {...}} -
            // the latter matches what GET /api/config returns, so a
            // round-trip get-edit-put doesn't require unwrapping.
            const Json::Value &cfg = json->isMember("config") ? (*json)["config"] : *json;

            std::string err;
            if (!ConfigStore::validate(cfg, err))
            {
                callback(jsonErr("invalid config: " + err, k400BadRequest));
                return;
            }

            std::string path = ConfigStore::resolvePath();
            if (!ConfigStore::write(path, cfg, err))
            {
                callback(jsonErr("failed to write config: " + err));
                return;
            }

            Json::Value j;
            j["ok"] = true;
            j["path"] = path;
            attachConfigWarning(j);
            callback(jsonOk(j));
        },
        {Put});

    app().registerHandler(
        "/api/config/device",
        [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
            auto json = req->getJsonObject();
            if (!json || !json->isMember("id"))
            {
                callback(jsonErr("request body must include at least 'id'", k400BadRequest));
                return;
            }

            Json::Value device(Json::objectValue);
            device["id"] = (*json)["id"];
            device["match"] =
                json->isMember("match") ? (*json)["match"] : Json::Value(Json::objectValue);
            if (json->isMember("grab"))
                device["grab"] = (*json)["grab"];
            device["bindings"] =
                json->isMember("bindings") ? (*json)["bindings"] : Json::Value(Json::objectValue);

            std::string err;
            if (!ConfigStore::validateDevice(device, err))
            {
                callback(jsonErr("invalid device: " + err, k400BadRequest));
                return;
            }

            std::string path = ConfigStore::resolvePath();
            Json::Value cfg;
            if (!ConfigStore::read(path, cfg, err))
            {
                callback(jsonErr("failed to read config: " + err));
                return;
            }
            if (!cfg.isMember("devices") || !cfg["devices"].isArray())
                cfg["devices"] = Json::Value(Json::arrayValue);

            const std::string newId = device["id"].asString();
            for (const auto &d : cfg["devices"])
            {
                if (d.isObject() && d.isMember("id") && d["id"].asString() == newId)
                {
                    callback(jsonErr("device id '" + newId + "' already exists in config",
                                      k400BadRequest));
                    return;
                }
            }
            cfg["devices"].append(device);

            if (!ConfigStore::write(path, cfg, err))
            {
                callback(jsonErr("failed to write config: " + err));
                return;
            }

            Json::Value j;
            j["ok"] = true;
            j["device"] = device;
            attachConfigWarning(j);
            callback(jsonOk(j));
        },
        {Post});

    app().registerHandler(
        "/api/config/device/{1}",
        [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback,
           const std::string &id) {
            auto json = req->getJsonObject();
            if (!json)
            {
                callback(jsonErr("request body must be valid JSON", k400BadRequest));
                return;
            }

            std::string path = ConfigStore::resolvePath();
            Json::Value cfg;
            std::string err;
            if (!ConfigStore::read(path, cfg, err))
            {
                callback(jsonErr("failed to read config: " + err));
                return;
            }

            bool found = false;
            if (cfg.isMember("devices") && cfg["devices"].isArray())
            {
                for (auto &d : cfg["devices"])
                {
                    if (d.isObject() && d.isMember("id") && d["id"].asString() == id)
                    {
                        found = true;
                        Json::Value updated = d;
                        if (json->isMember("match"))
                            updated["match"] = (*json)["match"];
                        if (json->isMember("grab"))
                            updated["grab"] = (*json)["grab"];

                        std::string devErr;
                        if (!ConfigStore::validateDevice(updated, devErr))
                        {
                            callback(jsonErr("invalid device: " + devErr, k400BadRequest));
                            return;
                        }
                        d = updated;
                        break;
                    }
                }
            }
            if (!found)
            {
                callback(jsonErr("device '" + id + "' not found in config", k404NotFound));
                return;
            }

            if (!ConfigStore::write(path, cfg, err))
            {
                callback(jsonErr("failed to write config: " + err));
                return;
            }

            Json::Value j;
            j["ok"] = true;
            attachConfigWarning(j);
            callback(jsonOk(j));
        },
        {Put});

    app().registerHandler(
        "/api/config/device/{1}",
        [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&callback,
           const std::string &id) {
            std::string path = ConfigStore::resolvePath();
            Json::Value cfg;
            std::string err;
            if (!ConfigStore::read(path, cfg, err))
            {
                callback(jsonErr("failed to read config: " + err));
                return;
            }

            bool found = false;
            Json::Value remaining(Json::arrayValue);
            if (cfg.isMember("devices") && cfg["devices"].isArray())
            {
                for (const auto &d : cfg["devices"])
                {
                    if (d.isObject() && d.isMember("id") && d["id"].asString() == id)
                    {
                        found = true;
                        continue;
                    }
                    remaining.append(d);
                }
            }
            if (!found)
            {
                callback(jsonErr("device '" + id + "' not found in config", k404NotFound));
                return;
            }
            cfg["devices"] = remaining;

            if (!ConfigStore::write(path, cfg, err))
            {
                callback(jsonErr("failed to write config: " + err));
                return;
            }

            Json::Value j;
            j["ok"] = true;
            attachConfigWarning(j);
            callback(jsonOk(j));
        },
        {Delete});

    app().registerHandler(
        "/api/events",
        [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&callback) {
            auto state = std::make_shared<EventStreamState>();

            auto resp = HttpResponse::newStreamResponse(
                [state](char *buf, std::size_t maxLen) -> std::size_t {
                    std::unique_lock<std::mutex> lock(state->mtx);
                    if (state->queue.empty())
                        state->cv.wait_for(lock, std::chrono::seconds(15));

                    std::string chunk;
                    if (!state->queue.empty())
                    {
                        chunk = "data: " + state->queue.front() + "\n\n";
                        state->queue.pop_front();
                    }
                    else
                    {
                        // SSE comment line - keeps the connection alive
                        // through proxies/idle timeouts without being
                        // treated as an event by the client.
                        chunk = ": keep-alive\n\n";
                    }
                    lock.unlock();

                    std::size_t n = std::min(chunk.size(), maxLen);
                    std::memcpy(buf, chunk.data(), n);
                    return n;
                });
            resp->setContentTypeCodeAndCustomString(CT_CUSTOM, "text/event-stream");
            resp->addHeader("Cache-Control", "no-cache");
            resp->addHeader("X-Accel-Buffering", "no");
            callback(resp);
        },
        {Get});

    LOG_INFO << "PointerForce web UI listening on http://0.0.0.0:8081";
    app().run();
    return 0;
}
