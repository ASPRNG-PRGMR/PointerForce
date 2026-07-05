#include "ConfigStore.h"
#include "ControlClient.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

// See the comment on kConfigPath in ConfigStore.h for why this is fixed
// rather than auto-detected.
const std::string ConfigStore::kConfigPath = "config/pointerforce.json";

namespace
{

// True if `a` and `b` point at the same file once both are resolved
// against the current working directory - i.e. the same comparison a
// human would do by running `readlink -f` on each and diffing. Falls back
// to plain string comparison if either path doesn't exist yet (e.g. the
// editor's file hasn't been created yet), since weakly_canonical can
// still throw on some unusual filesystem errors that aren't "file missing".
bool samePath(const std::string &a, const std::string &b)
{
    if (a == b)
        return true;
    std::error_code ecA, ecB;
    fs::path canonA = fs::weakly_canonical(fs::path(a), ecA);
    fs::path canonB = fs::weakly_canonical(fs::path(b), ecB);
    if (ecA || ecB)
        return false;
    return canonA == canonB;
}

}  // namespace

std::string ConfigStore::resolvePath()
{
    return kConfigPath;
}

std::string ConfigStore::daemonReportedPath()
{
    Json::Value req;
    req["cmd"] = "status";
    ControlClient::Result r = ControlClient::send(req);
    if (!r.transportOk || !r.response.get("ok", false).asBool())
        return "";  // daemon unreachable, or reachable but reported an error
    return r.response.get("config", "").asString();
}

std::string ConfigStore::daemonPathWarning()
{
    std::string daemonPath = daemonReportedPath();
    if (daemonPath.empty())
        return "";  // daemon unreachable - nothing to compare against

    if (samePath(daemonPath, kConfigPath))
        return "";

    return "the running daemon loaded its config from '" + daemonPath +
           "', but this editor reads/writes '" + kConfigPath +
           "'. Changes made here won't take effect on reload until the daemon is "
           "started with '-c " + kConfigPath + "' (or an absolute path to the same file).";
}

bool ConfigStore::read(const std::string &path, Json::Value &out, std::string &error)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        error = "cannot open '" + path +
                "' (does it exist? if you're running the bridge standalone, "
                "check that config/ is symlinked to PointerForce/config as make.sh sets up)";
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();

    Json::CharReaderBuilder builder;
    std::string parseErrors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(content.c_str(), content.c_str() + content.size(), &out, &parseErrors))
    {
        error = "invalid JSON: " + parseErrors;
        return false;
    }
    if (!out.isObject())
    {
        error = "top-level config must be a JSON object";
        return false;
    }
    return true;
}

bool ConfigStore::validateDevice(const Json::Value &device, std::string &error)
{
    if (!device.isObject())
    {
        error = "each device entry must be an object";
        return false;
    }
    if (!device.isMember("id") || !device["id"].isString() || device["id"].asString().empty())
    {
        error = "device.id is required and must be a non-empty string";
        return false;
    }
    const std::string id = device["id"].asString();

    if (device.isMember("match"))
    {
        if (!device["match"].isObject())
        {
            error = "device.match must be an object (with path, vendor_product, and/or name)";
            return false;
        }
    }
    else if (id != "*")
    {
        error = "device.match is required unless id is \"*\" (the wildcard device)";
        return false;
    }

    if (device.isMember("grab") && !device["grab"].isBool())
    {
        error = "device.grab must be a boolean";
        return false;
    }

    if (device.isMember("bindings"))
    {
        if (!device["bindings"].isObject())
        {
            error = "device.bindings must be an object mapping key names to shell commands";
            return false;
        }
        for (const auto &key : device["bindings"].getMemberNames())
        {
            if (!device["bindings"][key].isString())
            {
                error = "device.bindings['" + key + "'] must be a string (shell command)";
                return false;
            }
        }
    }

    return true;
}

bool ConfigStore::validate(const Json::Value &config, std::string &error)
{
    if (!config.isObject())
    {
        error = "config must be a JSON object";
        return false;
    }
    if (config.isMember("daemon") && !config["daemon"].isBool())
    {
        error = "'daemon' must be a boolean";
        return false;
    }
    if (config.isMember("debug") && !config["debug"].isBool())
    {
        error = "'debug' must be a boolean";
        return false;
    }
    if (config.isMember("control_socket") && !config["control_socket"].isString())
    {
        error = "'control_socket' must be a string";
        return false;
    }
    if (config.isMember("devices"))
    {
        if (!config["devices"].isArray())
        {
            error = "'devices' must be an array";
            return false;
        }
        std::set<std::string> seenIds;
        for (const auto &device : config["devices"])
        {
            std::string devErr;
            if (!validateDevice(device, devErr))
            {
                error = devErr;
                return false;
            }
            const std::string id = device["id"].asString();
            if (seenIds.count(id))
            {
                error = "duplicate device id: '" + id + "'";
                return false;
            }
            seenIds.insert(id);
        }
    }
    return true;
}

bool ConfigStore::write(const std::string &path, const Json::Value &config, std::string &error)
{
    std::string validationError;
    if (!validate(config, validationError))
    {
        error = "refusing to write invalid config: " + validationError;
        return false;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::string content = Json::writeString(builder, config);

    std::string tmpPath = path + ".tmp";
    {
        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            error = "cannot open '" + tmpPath + "' for writing (check directory permissions)";
            return false;
        }
        f << content;
        if (!f)
        {
            error = "write to '" + tmpPath + "' failed";
            return false;
        }
    }

    if (std::rename(tmpPath.c_str(), path.c_str()) != 0)
    {
        error = std::string("failed to replace '") + path + "': " + std::strerror(errno);
        std::remove(tmpPath.c_str());
        return false;
    }

    return true;
}
