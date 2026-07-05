# PointerForce

> A lightweight Linux input event mapper — bind any key or button on any number of devices to any shell command.

PointerForce listens to raw input devices via `libevdev`, maps key/button presses to shell commands per device, and executes them asynchronously. Phase 2 adds a multi-device pipeline with an event multiplexer. Phase 2.5 adds a live control plane — inspect and modify bindings at runtime with no restart.

---

## Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Building](#building)
- [Installation](#installation)
- [Configuration](#configuration)
- [Usage](#usage)
- [pfctl – Control Client](#pfctl--control-client)
- [Web UI](#web-ui)
- [Finding Your Device](#finding-your-device)
- [Permissions](#permissions)
- [Systemd Service](#systemd-service)
- [Runtime Reload](#runtime-reload)
- [Logging & Debugging](#logging--debugging)
- [Edge Cases & Error Handling](#edge-cases--error-handling)
- [Project Structure](#project-structure)
- [Phase 3 Roadmap](#phase-3-roadmap)

---

## Features

- **Multi-device** — any number of mice, keyboards, gamepads, macro pads, foot pedals running simultaneously
- **Device-aware bindings** — the same button on two different devices can trigger different commands
- **Flexible device matching** — match by name substring, USB vendor:product ID, or exact `/dev/input/eventN` path
- **Wildcard device** — a `"*"` device id matches any device for global bindings
- **Zero overhead event loop** — `libevdev` reads directly from `/dev/input/*`; each device runs in its own thread
- **Non-blocking execution** — commands run in detached child processes; threads never stall
- **Optional exclusive grab** — per-device; prevents other applications from seeing events
- **Daemon mode** — double-fork, PID file, systemd-compatible
- **Full in-process hot-reload** — SIGHUP or `pfctl reload` reloads config without restarting or dropping devices
- **Control plane** — Unix socket server; `pfctl` client for runtime status, inspection, and binding modification
- **Live event stream** — `pfctl events` shows every key press in real time
- **Web UI** — browser dashboard ([`frontend/`](./frontend/)) for status, device/binding management, live events, and config editing, without needing a terminal

---

## Requirements

| Dependency             | Package (Debian/Ubuntu)  |
|------------------------|--------------------------|
| libevdev               | `libevdev-dev`           |
| jsoncpp                | `libjsoncpp-dev`         |
| g++ >= 9 (C++17)       | `build-essential`        |
| Linux kernel >= 2.6.36 | (evdev input subsystem)  |

```bash
sudo apt install build-essential libevdev-dev libjsoncpp-dev
```

---

## Building

```bash
git clone https://github.com/yourname/pointerforce
cd pointerforce
make
```

This produces two binaries:

| Binary        | Purpose                              |
|---------------|--------------------------------------|
| `pointerforce` | Background daemon / foreground mode |
| `pfctl`        | Control client                      |

Debug build (symbols, verbose output, no optimisation):

```bash
make debug
```

Clean:

```bash
make clean
```

---

## Installation

```bash
sudo make install
```

Copies both binaries to `/usr/local/bin`, the default config to `/etc/pointerforce.json`, and registers the systemd unit.

```bash
sudo make uninstall
```

---

## Configuration

Edit `config/pointerforce.json` (or `/etc/pointerforce.json` after install).

```json
{
  "daemon": false,
  "debug": true,
  "control_socket": "/tmp/pointerforce.sock",

  "devices": [
    {
      "id": "gaming_mouse",
      "match": { "name": "Logitech G502" },
      "grab": false,
      "bindings": {
        "BTN_SIDE":    "notify-send 'Mouse' 'Side button'",
        "BTN_FORWARD": "playerctl next",
        "BTN_BACK":    "playerctl previous"
      }
    },
    {
      "id": "macro_pad",
      "match": { "vendor_product": "04d9:0348" },
      "grab": true,
      "bindings": {
        "KEY_F13": "scrot ~/screenshots/$(date +%s).png",
        "KEY_F14": "systemctl suspend"
      }
    },
    {
      "id": "foot_pedal",
      "match": { "path": "/dev/input/event7" },
      "grab": false,
      "bindings": {
        "BTN_LEFT":  "xdotool key ctrl+z",
        "BTN_RIGHT": "xdotool key ctrl+s"
      }
    }
  ]
}
```

### Top-level fields

| Field            | Type   | Default                        | Description                        |
|------------------|--------|--------------------------------|------------------------------------|
| `daemon`         | bool   | `false`                        | Run as background daemon           |
| `debug`          | bool   | `false`                        | Verbose logging                    |
| `control_socket` | string | `"/tmp/pointerforce.sock"`     | Path for the control Unix socket   |
| `devices`        | array  | required                       | List of device config blocks       |

### Device block fields

| Field      | Type   | Required | Description                                           |
|------------|--------|----------|-------------------------------------------------------|
| `id`       | string | yes      | Logical label — used in logs, bindings, and `pfctl`   |
| `match`    | object | yes*     | Criteria to identify the physical device (see below)  |
| `grab`     | bool   | no       | Exclusive grab — other apps won't see events          |
| `bindings` | object | no       | Map of `"KEY_NAME"` → `"shell command"`               |

\* `match` may be omitted only for the wildcard id `"*"`.

### Match criteria (first non-empty field wins)

| Field            | Example                | Description                                     |
|------------------|------------------------|-------------------------------------------------|
| `path`           | `"/dev/input/event7"`  | Exact event node path                           |
| `vendor_product` | `"046d:c08b"`          | USB vendor:product IDs (lowercase hex)          |
| `name`           | `"Logitech G502"`      | Substring of the kernel device name             |

Priority order: `path` → `vendor_product` → `name`.

Use `vendor_product` or `name` in production so paths survive reboots.

### Wildcard device

A device with `"id": "*"` acts as a catch-all. Any key that has no binding in the event's specific device will fall back to the wildcard table.

```json
{
  "id": "*",
  "match": {},
  "bindings": {
    "KEY_PAUSE": "notify-send 'Global' 'Pause pressed'"
  }
}
```

### Supported key names

PointerForce uses standard Linux key names from `<linux/input-event-codes.h>`:

**Mouse/Pointer:** `BTN_LEFT`, `BTN_RIGHT`, `BTN_MIDDLE`, `BTN_SIDE`, `BTN_EXTRA`, `BTN_FORWARD`, `BTN_BACK`

**Keyboard:** `KEY_A`–`KEY_Z`, `KEY_0`–`KEY_9`, `KEY_F1`–`KEY_F20`, `KEY_ENTER`, `KEY_SPACE`, `KEY_ESC`, `KEY_TAB`, `KEY_BACKSPACE`, `KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, `KEY_RIGHT`, `KEY_HOME`, `KEY_END`, `KEY_PAGEUP`, `KEY_PAGEDOWN`, `KEY_INSERT`, `KEY_DELETE`, `KEY_LEFTSHIFT`, `KEY_RIGHTSHIFT`, `KEY_LEFTCTRL`, `KEY_RIGHTCTRL`, `KEY_LEFTALT`, `KEY_RIGHTALT`, `KEY_LEFTMETA`, `KEY_RIGHTMETA`, `KEY_CAPSLOCK`, `KEY_PAUSE`, `KEY_SCROLLLOCK`, `KEY_NUMLOCK`

**Numpad:** `KEY_KP0`–`KEY_KP9`, `KEY_KPENTER`, `KEY_KPPLUS`, `KEY_KPMINUS`, `KEY_KPASTERISK`, `KEY_KPSLASH`, `KEY_KPDOT`

**Media:** `KEY_MUTE`, `KEY_VOLUMEUP`, `KEY_VOLUMEDOWN`, `KEY_PLAYPAUSE`, `KEY_NEXTSONG`, `KEY_PREVIOUSSONG`

**Gamepad:** `BTN_SOUTH`, `BTN_EAST`, `BTN_NORTH`, `BTN_WEST`, `BTN_TL`, `BTN_TR`, `BTN_TL2`, `BTN_TR2`, `BTN_START`, `BTN_SELECT`, `BTN_THUMBL`, `BTN_THUMBR`, `BTN_MODE`

For any unlisted key use `evtest` to find its name, then add it directly in your config.

---

## Usage

### Foreground (debug) mode

```bash
./pointerforce -f
./pointerforce -f -c /path/to/config.json
```

Output:

```
[config] devices       : 2
[device] Probed: Logitech G502 HERO (/dev/input/event5)
[devicemgr] Matched 'gaming_mouse' → /dev/input/event5 (Logitech G502 HERO)
[device] Probed: OLKB Planck (/dev/input/event8)
[devicemgr] Matched 'macro_pad' → /dev/input/event8 (OLKB Planck)
[mapper] Loaded 6 binding(s) across 2 device(s).
[ctrl] Control server: /tmp/pointerforce.sock
[main] Running with 2 device(s).
[mux] Started. Waiting for events from 2 device(s).
[mux] gaming_mouse → BTN_FORWARD (159)
[mux] [gaming_mouse] Execute: playerctl next
```

### Daemon mode

```bash
./pointerforce -d
```

Or set `"daemon": true` in config.

### CLI flags

| Flag        | Description                                          |
|-------------|------------------------------------------------------|
| `-c <file>` | Config file path (default: `config/pointerforce.json`) |
| `-d`        | Force daemon mode                                    |
| `-f`        | Force foreground + debug mode                        |
| `-v`        | Print version and exit                               |
| `-h`        | Show help                                            |

---

## pfctl – Control Client

`pfctl` communicates with the running daemon via the control socket. All commands require the daemon to be running.

### System overview

```bash
pfctl status
```
```
Version    : 0.2.0
Running    : yes
Devices    : 2
Bindings   : 6
Config     : /etc/pointerforce.json
```

### List active devices

```bash
pfctl devices
```
```
  [gaming_mouse]
    path   : /dev/input/event5
    name   : Logitech G502 HERO
    active : yes
  [macro_pad]
    path   : /dev/input/event8
    name   : OLKB Planck
    active : yes
```

### List bindings

```bash
pfctl bindings               # all devices
pfctl bindings gaming_mouse  # filtered by device id
```
```
  [gaming_mouse] BTN_FORWARD  →  playerctl next
  [gaming_mouse] BTN_BACK     →  playerctl previous
  [macro_pad]    KEY_F13      →  scrot ~/screenshots/$(date +%s).png
```

### Add or replace a binding at runtime

```bash
pfctl bind gaming_mouse BTN_MIDDLE "xdotool click 2"
```

Takes effect immediately — no restart required.

### Remove a binding at runtime

```bash
pfctl unbind gaming_mouse BTN_MIDDLE
```

### Live event stream

```bash
pfctl events
```
```
Listening for events (Ctrl-C to quit)...
[gaming_mouse] BTN_FORWARD  →  playerctl next
[macro_pad]    KEY_F13      →  scrot ~/screenshots/...
[gaming_mouse] BTN_SIDE
```

### Hot-reload config from disk

```bash
pfctl reload
```

Equivalent to sending SIGHUP. Reloads `pointerforce.json` and reinitialises all bindings in-process. Running device threads are unaffected.

---

## Web UI

PointerForce ships with a browser-based dashboard, [`frontend/`](./frontend/) —
this is the "Web dashboard" item from the Phase 3 roadmap below, now built
rather than planned. It's not a bolt-on extra maintained separately: it's
part of this repository, this README, and this system, the same as `pfctl`
is.

(This section used to be `frontend/README.md`, its own separate file. It's
merged in here now — there's one README for the whole system. If you still
have a copy of that file lying around, delete it; this is the current
version.)

### How it works

`frontend/` is a Drogon (C++) backend bridge plus a static HTML/CSS/JS
frontend (no build step). The bridge connects directly to the same Unix
control socket `pfctl` does, speaking the JSON-lines protocol in
`control.cpp` itself via `ControlClient` — not by shelling out to `pfctl`
as a subprocess. So it needs the `pointerforce` daemon running, but not
`pfctl` installed on whatever machine serves the dashboard.

```
 browser  <--HTTP/SSE-->  frontend bridge (Drogon)  <--Unix socket, JSON-lines-->  pointerforce daemon
```

Connecting to the control socket directly rather than shelling out to
`pfctl` fixes two concrete things, not just "one less moving part":

- No dependency on `pfctl` being installed or on `PATH` for the bridge to
  work at all.
- No scraping `pfctl`'s human-readable text output. Every field in the
  HTTP API comes straight from the daemon's own JSON, so a future change
  to `pfctl`'s pretty-printing can't silently break this bridge — that was
  a real, documented risk with the earlier `pfctl`-shelling design this
  replaced.

It also means the bridge can read `control_socket` out of the same config
file `ConfigStore` already resolves, rather than being stuck with
`pfctl.cpp`'s hardcoded `/tmp/pointerforce.sock` — see "Which config file
gets edited" below for why that mismatch mattered.

### Requirements

- The `pointerforce` daemon already built and running (see Building/Usage
  above) — its control socket is all this needs.
- Build tools: `cmake`, a C++17 compiler, and the Drogon framework.

On Debian/Ubuntu:

```bash
sudo apt install build-essential cmake libdrogon-dev libjsoncpp-dev \
                  libpq-dev libsqlite3-dev libmysqlclient-dev libhiredis-dev \
                  libyaml-cpp-dev
```

(Drogon's CMake config pulls in the Postgres/MySQL/SQLite/Redis client
headers even though this only uses Drogon's HTTP server — that's Drogon's
own `find_package`, not something this project needs directly.)

### Building & running

```bash
cd frontend
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
./pointerforce-frontend
```

Then open `http://localhost:8081`.

The binary must be run from `frontend/build/` (or any directory where
`../webui` resolves to `frontend/webui`) since it serves the static
frontend from a relative path. If you want to run it from elsewhere, edit
the `app().setDocumentRoot(...)` call in `main.cpp`. `make.sh`, which now
lives directly in `frontend/` (not inside `build/` — it creates `build/`
itself now), automates this build, sets up the shared `config/` symlink
described below, and generates a `run_frontend` launcher at the project
root so you don't have to `cd` there by hand every time.

### What the UI does

| Panel        | What it shows / does                                              |
|--------------|---------------------------------------------------------------------|
| Top bar      | Daemon status, version, device/binding counts, hot-reload button   |
| Devices      | Every matched device, its path/name, and whether it's active; click one to filter the bindings table |
| Bindings     | Every key → command binding; add new ones inline, or unbind existing ones; an interactive on-screen mouse doubles as the binding editor — click a zone to bind it |
| Live events  | A real-time feed straight from the control socket's `events` stream, with the matching binding row flashing when it fires |
| Config tab   | Structured editor for `pointerforce.json`'s device blocks (id, match, grab) — add/edit/delete — plus a raw-JSON toggle for anything else (`daemon`, `debug`, `control_socket`, bulk binding edits). Saves write to disk only; a separate "reload daemon" button applies them and shows the result. |

Adding/removing bindings takes effect immediately, the same as running
`pfctl bind` / `pfctl unbind` by hand — no restart, no daemon interruption.

### API reference

All endpoints are relative to the bridge server (default `:8081`).

| Method | Path            | Body                                   | Notes                        |
|--------|-----------------|-----------------------------------------|-------------------------------|
| GET    | `/api/status`   | —                                        | `{"cmd":"status"}` over the control socket, as JSON |
| GET    | `/api/devices`  | —                                        | `{"cmd":"devices"}`, as JSON  |
| GET    | `/api/bindings` | —                                        | `{"cmd":"bindings"}`, as JSON |
| POST   | `/api/bind`     | `{ "device", "key", "command" }`         | `{"cmd":"bind", ...}`         |
| POST   | `/api/unbind`   | `{ "device", "key" }`                    | `{"cmd":"unbind", ...}`       |
| GET    | `/api/events`   | —                                        | Server-Sent Events, sourced from the control socket's live `events` stream |
| POST   | `/api/reload`   | —                                        | `{"cmd":"reload"}`            |
| GET    | `/api/config`   | —                                        | Reads and parses `pointerforce.json`, returns `{ path, config, warning? }` |
| PUT    | `/api/config`   | `{ "config": {...} }` or raw config object | Validates shape, writes atomically. Does **not** reload the daemon. |
| POST   | `/api/config/device`   | `{ "id", "match", "grab"?, "bindings"? }` | Appends a new device block to config |
| PUT    | `/api/config/device/:id` | `{ "match"?, "grab"? }`         | Edits an existing device block's match/grab. Bindings still go through `/api/bind` (live). |
| DELETE | `/api/config/device/:id` | —                                | Removes a device block from config |

All socket communication goes through `ControlClient` (`frontend/services/`),
a plain `AF_UNIX` client speaking the same JSON-lines protocol
`control.cpp` implements — connect, send one `{"cmd": ...}` line, read one
`{"ok": ...}` line back, close. `events` is the exception: after the
request line, the connection stays open and the daemon streams one event
object per line until the client disconnects, which is exactly what backs
the SSE endpoint above.

### Which config file gets edited

The config-file endpoints (`GET`/`PUT /api/config` and everything under
`/api/config/device`) don't go through the control socket at all — there's
no command for structural edits, only live bindings — so they read/write
`pointerforce.json` directly, at a **fixed** path: `config/pointerforce.json`,
relative to wherever the bridge process's working directory is (same
resolution rule as `setDocumentRoot("../webui")` for the static frontend).

This assumes the deployment layout where a `PointerForce/` root holds the
daemon, the web UI, and a single shared `config/` directory:

```
PointerForce/
├── config/
│   └── pointerforce.json
├── run_frontend                 ← launcher; cd's into frontend/build before exec
└── frontend/
    ├── webui/                   (static index.html / app.js / style.css)
    └── build/
        ├── config -> ../../config   (symlink, set up by make.sh)
        └── pointerforce-frontend    (this binary)
```

Run the binary from anywhere else without that symlink in place, and
`/api/config` will fail with a clear "cannot open 'config/pointerforce.json'"
rather than silently editing the wrong file.

This was previously auto-detected per-request via `pfctl status`'s
`Config:` line, which meant the file the editor wrote to and the file the
daemon actually loaded (whatever `-c` it happened to be started with)
were two independent things that could silently drift apart. Pinning both
to the same relative path removes that drift *if* the daemon is started
with `-c config/pointerforce.json` (or an absolute path to the same file
through the symlink) — but they can still disagree if the daemon is
started some other way, so this is checked, not just assumed: every
config response includes an optional `warning` field, set whenever the
control socket is reachable and `status` reports a config path that
doesn't match the one this editor just used (`ConfigStore::daemonPathWarning()`).
No warning means either they agree, or the daemon isn't running (nothing
to check yet) — not a guarantee everything's correctly wired the first time.

**One known blind spot in that check, worth knowing about:** the
comparison canonicalizes both paths against *this bridge process's* CWD.
If the daemon reports a relative path too (e.g. it was also started with
a relative `-c`, from a different working directory than the bridge's),
both sides can print the identical string while actually pointing at two
different files on disk — and the check will see matching strings and
report no warning. This is exactly the failure mode that cost real
debugging time before it was caught live: `pfctl status` said
`config/pointerforce.json`, which looked fine, but which file that
actually was depended entirely on what directory the daemon happened to
be launched from. Always launch the daemon with `-c` pointed at an
**absolute** path if you want this check to be trustworthy, not just a
relative one that happens to render the same.

If you need per-instance or environment-driven config paths later,
`ConfigStore::resolvePath()` is the one place that decides this — it's a
single method specifically so that's a self-contained change.

### Known limitations (minimal-viable, by design)

- No auth — this is meant to sit behind your own network/reverse proxy the
  same way you'd guard SSH or direct access to the control socket, not to
  be exposed publicly.
- No HTTPS — terminate TLS in front of it (nginx/Caddy) if you need it
  reachable outside localhost.
- Config-path mismatch detection has a real blind spot when both the
  bridge and the daemon are launched with *relative* `-c` paths from
  different working directories — see "Which config file gets edited"
  above. Not theoretical; this is exactly what happened during initial
  testing.
- No systemd unit included yet for the bridge itself — run it under
  `screen`/`tmux`, or write a small unit file analogous to
  `pointerforce.service` if you want it to survive reboots.

### Next steps

**Windows port.** The near-term goal is a Windows build of PointerForce
itself, at which point this web UI's `ControlClient` would connect
directly to whatever control-plane socket that build exposes — no `pfctl`
involved anywhere, on either platform. One upside worth noting: Windows 10
(build 17063+) and Windows 11 both support `AF_UNIX` sockets via
`afunix.h`, so `ControlClient` as written might port with fairly small
changes rather than needing a named-pipe rewrite — untested, but worth
checking before assuming a bigger rewrite is needed. Rough mapping from
the current Linux/evdev design for everything else:

| Linux (evdev) | Windows |
|---|---|
| `/dev/input/eventN`<br>(fd, blocking read) | `HANDLE` from Raw Input,<br>delivered via `WM_INPUT` messages |
| `match.vendor_product`<br>`046d:c08b` | VID/PID parsed from the Raw Input device name,<br>or `RID_DEVICE_INFO.dwVendorId` / `dwProductId` |
| `match.path`<br>`/dev/input/by-id/...` | Raw Input device instance path, e.g.<br>`\\?\HID#VID_046D&PID_C08B&MI_00#7&2a1e3f9&0&0000#{...}` |
| `match.name` | HID product string via SetupAPI,<br>or the friendly device name |
| `grab: true`<br>(exclusive grab) | **No direct equivalent.**<br>Raw Input observes input but cannot suppress it.<br><br>For suppression use:<br>- `WH_MOUSE_LL` / `WH_KEYBOARD_LL` (user-mode)<br>- Signed kernel filter driver (e.g. Interception) |
| Reader thread<br>per device | Single message-loop thread processing `WM_INPUT`.<br>The downstream mux → mapper → executor pipeline remains unchanged. |  

\
Device identification specifically is confirmed feasible:
`GetRawInputDeviceList()` enumerates connected devices, and
`GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, ...)` returns the HID
instance path above — VID/PID and a port-stable path in one string, same
role as `match.vendor_product`/`match.path` today. Two identical devices
off the same production line still collide on VID/PID, same caveat as
Linux, same reason the `path` tiebreaker exists.

Before committing design time: write a small scratch program that
enumerates connected devices and prints VID/PID + instance path, to
confirm on real hardware which of the target devices resolve to distinct,
stable identifiers. (See the Phase 3 Roadmap below too — this Windows work
and several daemon-side items there are related but independent efforts.)

**Other:**

- Wire the final visual design into `frontend/` once ready — current styling
  is a placeholder dark-terminal theme, not the intended final look.
- Add a systemd unit for the bridge server itself (see Known limitations
  above).

---

## Finding Your Device

List all input devices:

```bash
cat /proc/bus/input/devices
```

Identify the right node and see live key codes:

```bash
sudo evtest
```

Find USB vendor and product IDs for `vendor_product` matching:

```bash
lsusb
# e.g. Bus 001 Device 007: ID 046d:c08b Logitech, Inc.
# → "046d:c08b"
```

Stable paths that survive reboots:

```bash
ls -l /dev/input/by-id/
ls -l /dev/input/by-path/
```

---

## Permissions

`/dev/input/*` requires `root` or membership in the `input` group.

```bash
sudo usermod -aG input $USER
# Log out and back in.
```

### Optional: udev rule

```
# /etc/udev/rules.d/99-pointerforce.rules
SUBSYSTEM=="input", ATTRS{idVendor}=="046d", ATTRS{idProduct}=="c08b", MODE="0660", GROUP="input"
```

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

---

## Systemd Service

```bash
sudo systemctl enable --now pointerforce
sudo systemctl status pointerforce
sudo journalctl -u pointerforce -f
sudo systemctl stop pointerforce
```

To run as a non-root user add `User=yourname` and `Group=input` to the `[Service]` section of `/etc/systemd/system/pointerforce.service`.

---

## Runtime Reload

Three equivalent ways to trigger an in-process config reload:

```bash
scripts/reload.sh
kill -HUP $(cat /tmp/pointerforce.pid)
pfctl reload
```

The multiplexer detects the reload flag between events, re-reads `pointerforce.json`, and reinitialises the mapper. Device reader threads keep running throughout — no events are lost.

---

## Logging & Debugging

In foreground/debug mode all output goes to stdout.

In daemon mode output is appended to `logs/pointerforce.log` (or `/var/log/pointerforce.log` when installed).

```bash
tail -f /var/log/pointerforce.log
```

### Diagnostic tools

| Tool              | Purpose                                               |
|-------------------|-------------------------------------------------------|
| `evtest`          | Identify devices, inspect raw events, find key codes  |
| `pfctl events`    | Live event stream from inside PointerForce            |
| `pfctl status`    | Runtime overview                                      |
| `strace`          | Trace syscalls — diagnose open/ioctl/grab failures    |
| `lsusb`           | Find USB vendor/product IDs for matching              |
| `udevadm monitor` | Watch device hotplug events                           |

---

## Edge Cases & Error Handling

| Situation                          | Behaviour                                                      |
|------------------------------------|----------------------------------------------------------------|
| No configured devices found        | Exits with error at startup                                    |
| Device path not found at startup   | That device is skipped; others start normally                  |
| Invalid JSON config                | Exits with parse error; reload failure keeps current config    |
| Unknown key name in bindings       | Logs warning, skips that binding, continues                    |
| Device disconnected at runtime     | Reader thread detects EIO, marks device inactive, exits thread |
| Command execution failure          | fork/exec errors logged; multiplexer continues                 |
| Duplicate instance                 | Detects live PID file, exits with message                      |
| SIGTERM / SIGINT                   | `g_running = false`; multiplexer exits; all threads join       |
| SIGHUP / `pfctl reload`            | `g_reload = true`; config reloaded between events in-process  |
| Exclusive grab failure             | Logs warning, continues without grab (non-fatal)               |
| Empty bindings on a device         | Logs warning, starts anyway                                    |
| Control socket unavailable         | Logs warning, daemon continues without control plane           |
| Concurrent `pfctl` clients         | Each handled in a separate detached thread                     |

---

## Project Structure

```
PointerForce/
├── src/
│   ├── main.cpp              Entry point, CLI parsing, init flow
│   ├── config.cpp            JSON config loader (multi-device)
│   ├── device.cpp            libevdev open/grab/close + match/vendor_product
│   ├── devicemanager.cpp     Scan /dev/input, open matched devices, reader threads
│   ├── eventqueue.cpp        Thread-safe multi-producer / single-consumer queue
│   ├── multiplexer.cpp       Pop queue → device-aware lookup → execute + notify
│   ├── mapper.cpp            Per-device binding tables, thread-safe, wildcard
│   ├── executor.cpp          Non-blocking fork/exec command runner
│   ├── daemon.cpp            Daemonise, PID file, signal handlers
│   ├── control.cpp           Unix socket control server (JSON-lines)
│   └── pfctl.cpp             Control client binary
├── include/
│   ├── common.hpp            Shared types (Config, DeviceConfig, InputEvent), globals
│   ├── config.hpp
│   ├── device.hpp
│   ├── devicemanager.hpp
│   ├── eventqueue.hpp
│   ├── multiplexer.hpp
│   ├── mapper.hpp
│   ├── executor.hpp
│   ├── daemon.hpp
│   └── control.hpp
├── config/
│   └── pointerforce.json      Example configuration
├── scripts/
│   └── reload.sh              Send SIGHUP to running daemon
├── logs/
│   └── pointerforce.log       Runtime log (daemon mode)
├── service    
│   └── pointerforce.service   systemd unit file
├── Makefile
├── README.md
└── frontend/                  Browser dashboard — see Web UI above
```

### Architecture

```
 /dev/input/*
      │
      ▼ (scan + match against config)
 ┌─────────────────────────────┐
 │       DeviceManager         │
 │  ┌──────────┐ ┌──────────┐  │
 │  │ Device A │ │ Device B │  │   one reader thread per device
 │  └────┬─────┘ └────┬─────┘  │
 └───────┼────────────┼────────┘
         │            │
         └─────┬──────┘
               ▼
         EventQueue          (thread-safe)
               │
               ▼
      EventMultiplexer
               │
         ┌─────┴──────────┐
         ▼                ▼
       Mapper          Observers  ──→  ControlServer event-stream clients
  (device-aware        (pfctl events)
   binding lookup)
         │
         ▼
      Executor
   (fork + exec)
         │
         ▼
   shell commands

         ┌─────────────────────┐
         │    ControlServer    │  (Unix socket / JSON-lines)
         │  status / devices   │
         │  bindings / bind    │
         │  unbind / reload    │
         │  events (stream)    │
         └─────────────────────┘
               ▲
               │
        ┌──────┴───────┐
        │              │
     pfctl           frontend
  (terminal)     (ControlClient, direct
                  socket connection —
                  no pfctl involved)
```

---

## Phase 3 Roadmap

- **uinput injection** — synthesise and remap events at the virtual device level
- **Event suppression** — swallow events before they reach other applications (requires exclusive grab + uinput re-emit)
- **Combo / sequence bindings** — chords, tap-vs-hold, double-tap, hold-repeat
- **Per-application profiles** — activate binding sets based on the focused window (`_NET_ACTIVE_WINDOW`)
- **eBPF input handler** — kernel-level interception for zero-latency suppression
- **Device hotplug** — automatically open newly connected devices matching config
- ~~**Web dashboard** — browser-based UI served over HTTP for remote control~~ — done, see [Web UI](#web-ui)
