#pragma once

#include <json/json.h>
#include <string>

// Reads, validates, and atomically writes PointerForce's config file
// (pointerforce.json). This is the one part of the bridge that touches the
// daemon's on-disk state directly rather than going through pfctl - there's
// no pfctl subcommand for structural config edits (adding/removing device
// blocks), only for live bindings.
//
// Nothing here auto-reloads the daemon. Callers write the file, then the
// frontend explicitly hits POST /api/reload afterwards - a deliberate
// diff-then-confirm step, and it means an in-progress edit never has a
// chance to be picked up half-written.
class ConfigStore
{
public:
    // The config file this editor always reads/writes: "config/pointerforce.json",
    // relative to wherever the bridge process's CWD is - same resolution
    // rule as `app().setDocumentRoot("../webui")` in main.cpp.
    //
    // In the current PointerForce/ deployment layout, the bridge is always
    // launched (via the generated `run_webui` wrapper) from
    // `pointerforce-webui/backend/build/`, and `make.sh` symlinks
    // `backend/build/config` -> `PointerForce/config`. So this resolves to
    // the one shared `PointerForce/config/pointerforce.json` that the
    // daemon should also be pointed at via `pointerforce -c
    // /absolute/path/to/PointerForce/config/pointerforce.json`.
    //
    // Deliberately fixed rather than auto-detected via `pfctl status` for
    // now: auto-detection was the source of the "two independent
    // resolution paths that aren't guaranteed to agree" problem (see
    // DEPLOYMENT.md) - pfctl's self-reported path reflects whatever `-c`
    // the daemon happened to be started with, which is a different source
    // of truth than "the file this editor is pointed at." Pinning both to
    // the same relative path, backed by the same symlink, removes the
    // disagreement instead of trying to detect it after the fact.
    static const std::string kConfigPath;

    // Always returns kConfigPath. Kept as a method (rather than having
    // callers just use kConfigPath directly) so call sites don't need to
    // change if this ever needs to become configurable again - e.g. an
    // env var or CLI flag for multi-instance setups.
    static std::string resolvePath();

    // Best-effort: asks the *running daemon* (over the control socket, via
    // ControlClient) what config path it actually loaded. Returns "" if
    // the daemon isn't reachable. This is informational only - see
    // daemonPathWarning() below - and is never used to decide what file
    // to read or write.
    static std::string daemonReportedPath();

    // If the daemon is reachable and reports a config path that doesn't
    // match `resolvePath()`, returns a human-readable warning describing
    // the mismatch (safe to surface directly in an API response). Returns
    // "" if the daemon is unreachable or the paths agree.
    static std::string daemonPathWarning();

    // Reads and parses the config file at `path`. Returns false and sets
    // `error` on any failure: missing file, unreadable, or invalid JSON.
    static bool read(const std::string &path, Json::Value &out, std::string &error);

    // Validates a config document's shape. This is intentionally a shape
    // check (right types, required fields present, ids unique) rather than
    // a semantic one - it won't catch e.g. a bogus key name, the same way
    // pointerforce itself only warns and skips on that at load time.
    static bool validate(const Json::Value &config, std::string &error);

    // Validates a single device block. Exposed separately so the
    // POST /device and PUT /device/:id endpoints can validate just the
    // piece the user submitted before merging it into the full document.
    static bool validateDevice(const Json::Value &device, std::string &error);

    // Serializes `config` and writes it to `path` atomically (write to a
    // temp file in the same directory, then rename over the original) so a
    // crash or concurrent read never sees a half-written file.
    static bool write(const std::string &path, const Json::Value &config, std::string &error);
};

