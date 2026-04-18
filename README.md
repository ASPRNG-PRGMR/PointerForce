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
pointerforce/
├── src/
│   ├── main.cpp            Entry point, CLI parsing, init flow
│   ├── config.cpp          JSON config loader (multi-device)
│   ├── device.cpp          libevdev open/grab/close + match/vendor_product
│   ├── devicemanager.cpp   Scan /dev/input, open matched devices, reader threads
│   ├── eventqueue.cpp      Thread-safe multi-producer / single-consumer queue
│   ├── multiplexer.cpp     Pop queue → device-aware lookup → execute + notify
│   ├── mapper.cpp          Per-device binding tables, thread-safe, wildcard
│   ├── executor.cpp        Non-blocking fork/exec command runner
│   ├── daemon.cpp          Daemonise, PID file, signal handlers
│   ├── control.cpp         Unix socket control server (JSON-lines)
│   └── pfctl.cpp           Control client binary
├── include/
│   ├── common.hpp          Shared types (Config, DeviceConfig, InputEvent), globals
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
│   └── pointerforce.json   Example configuration
├── scripts/
│   └── reload.sh           Send SIGHUP to running daemon
├── logs/
│   └── pointerforce.log    Runtime log (daemon mode)
├── pointerforce.service    systemd unit file
├── Makefile
└── README.md
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
            pfctl
```

---

## Phase 3 Roadmap

- **uinput injection** — synthesise and remap events at the virtual device level
- **Event suppression** — swallow events before they reach other applications (requires exclusive grab + uinput re-emit)
- **Combo / sequence bindings** — chords, tap-vs-hold, double-tap, hold-repeat
- **Per-application profiles** — activate binding sets based on the focused window (`_NET_ACTIVE_WINDOW`)
- **eBPF input handler** — kernel-level interception for zero-latency suppression
- **Web dashboard** — browser-based UI served over HTTP for remote control
- **Device hotplug** — automatically open newly connected devices matching config
