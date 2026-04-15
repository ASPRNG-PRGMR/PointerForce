# PointerForce

> A lightweight Linux input event mapper — bind any key or button to any shell command.

PointerForce listens to a raw input device via `libevdev`, maps key/button presses to shell commands, and executes them asynchronously. It runs quietly in the background as a daemon, or interactively in foreground debug mode.

---

## Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Building](#building)
- [Installation](#installation)
- [Configuration](#configuration)
- [Usage](#usage)
- [Finding Your Device](#finding-your-device)
- [Permissions](#permissions)
- [Systemd Service](#systemd-service)
- [Runtime Reload](#runtime-reload)
- [Logging & Debugging](#logging--debugging)
- [Edge Cases & Error Handling](#edge-cases--error-handling)
- [Project Structure](#project-structure)
- [Phase 2 Roadmap](#phase-2-roadmap)

---

## Features

- **Zero overhead** event loop via `libevdev` — reads directly from `/dev/input/*`
- **Any device** — mice, keyboards, gamepads, macro pads, foot pedals, custom HID
- **Simple JSON config** — human-readable key names (`BTN_SIDE`, `KEY_F13`, etc.)
- **Non-blocking execution** — commands run in detached child processes; the loop never stalls
- **Optional exclusive grab** — prevent other applications from seeing the same events
- **Daemon mode** — double-fork, PID file, systemd-compatible
- **Graceful shutdown** — SIGTERM / SIGINT / SIGHUP all handled cleanly

---

## Requirements

| Dependency | Package (Debian/Ubuntu)       |
|------------|-------------------------------|
| libevdev   | `libevdev-dev`                |
| jsoncpp    | `libjsoncpp-dev`              |
| g++ >= 9   | `build-essential`             |
| Linux kernel >= 2.6.36 | (evdev input subsystem) |

```bash
sudo apt install build-essential libevdev-dev libjsoncpp-dev
```

---

## Building

```bash
git clone https://github.com/yourname/PointerForce
cd PointerForce
make
```

For a debug build with symbols and verbose output:

```bash
make debug
```

Clean build artefacts:

```bash
make clean
```

---

## Installation

```bash
sudo make install
```

This copies the binary to `/usr/local/bin/PointerForce`, the default config to `/etc/PointerForce.json`, and registers the systemd unit.

To uninstall completely:

```bash
sudo make uninstall
```

---

## Configuration

Edit `config/PointerForce.json` (or `/etc/PointerForce.json` after install):

```json
{
  "device": "/dev/input/event5",
  "grab": false,
  "daemon": false,
  "debug": true,
  "bindings": {
    "BTN_SIDE":    "notify-send 'PointerForce' 'Side button pressed'",
    "BTN_EXTRA":   "xdg-open https://example.com",
    "BTN_FORWARD": "playerctl next",
    "BTN_BACK":    "playerctl previous",
    "KEY_F13":     "scrot ~/screenshots/$(date +%s).png",
    "KEY_F14":     "systemctl suspend"
  }
}
```

### Fields

| Field      | Type    | Required | Default | Description |
|------------|---------|----------|---------|-------------|
| `device`   | string  | yes      | —       | Path to input device, e.g. `/dev/input/event5` |
| `grab`     | bool    | no       | `false` | Exclusive grab — other apps won't see events |
| `daemon`   | bool    | no       | `false` | Run as background daemon |
| `debug`    | bool    | no       | `false` | Verbose logging to stdout |
| `bindings` | object  | yes      | —       | Map of `"KEY_NAME"` -> `"shell command"` |

### Supported Key Names

PointerForce uses standard Linux key names from `<linux/input-event-codes.h>`:

**Mouse/Pointer**
`BTN_LEFT`, `BTN_RIGHT`, `BTN_MIDDLE`, `BTN_SIDE`, `BTN_EXTRA`, `BTN_FORWARD`, `BTN_BACK`

**Keyboard**
`KEY_A`-`KEY_Z`, `KEY_0`-`KEY_9`, `KEY_F1`-`KEY_F12`, `KEY_ENTER`, `KEY_SPACE`, `KEY_ESC`, `KEY_TAB`, `KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, `KEY_RIGHT`, `KEY_HOME`, `KEY_END`, `KEY_PAGEUP`, `KEY_PAGEDOWN`, `KEY_INSERT`, `KEY_DELETE`, `KEY_LEFTSHIFT`, `KEY_RIGHTSHIFT`, `KEY_LEFTCTRL`, `KEY_RIGHTCTRL`, `KEY_LEFTALT`, `KEY_RIGHTALT`, `KEY_LEFTMETA`, `KEY_RIGHTMETA`

**Media**
`KEY_MUTE`, `KEY_VOLUMEUP`, `KEY_VOLUMEDOWN`, `KEY_PLAYPAUSE`, `KEY_NEXTSONG`, `KEY_PREVIOUSSONG`

**Gamepad**
`BTN_SOUTH`, `BTN_EAST`, `BTN_NORTH`, `BTN_WEST`, `BTN_TL`, `BTN_TR`, `BTN_START`, `BTN_SELECT`

To look up a code for an unlisted key, use `evtest` (see [Finding Your Device](#finding-your-device)).

---

## Usage

### Foreground (debug) mode

```bash
./PointerForce -f
# or
./PointerForce -f -c /path/to/your.json
```

Press buttons — you'll see real-time output:

```
[config] device     : /dev/input/event5
[config] grab       : false
[config] bindings   : 4 entries
[device] Opened: Logitech G502 HERO (/dev/input/event5)
[mapper] Loaded 4 binding(s).
[event] Loop started. Listening...
[event] KEY BTN_SIDE (277) pressed
[event] Executing: notify-send 'PointerForce' 'Side button pressed'
[executor] Spawned PID 12483 for: notify-send ...
```

### Daemon mode

```bash
./PointerForce -d
```

Or set `"daemon": true` in config and run without flags.

### CLI flags

| Flag         | Description |
|--------------|-------------|
| `-c <file>`  | Path to config JSON (default: `config/PointerForce.json`) |
| `-d`         | Force daemon mode (overrides config) |
| `-f`         | Force foreground + debug mode (overrides config) |
| `-v`         | Print version and exit |
| `-h`         | Show help |

---

## Finding Your Device

List all input devices:

```bash
cat /proc/bus/input/devices
```

Or use `evtest` to identify the right event node and see live key codes:

```bash
sudo evtest
```

Select your device from the list, then press buttons to see their names and codes. Use those names directly in your `bindings` config.

Stable device paths (survive reboots):

```bash
ls -l /dev/input/by-id/
ls -l /dev/input/by-path/
```

---

## Permissions

By default, `/dev/input/*` requires either `root` or the `input` group.

Add your user to the `input` group (no root needed at runtime):

```bash
sudo usermod -aG input $USER
# Log out and back in for the change to take effect
```

Verify:

```bash
ls -l /dev/input/event5
# crw-rw---- 1 root input ... /dev/input/event5
```

### Optional: udev rule

For a specific device to always be accessible without group membership:

```
# /etc/udev/rules.d/99-PointerForce.rules
SUBSYSTEM=="input", ATTRS{idVendor}=="046d", ATTRS{idProduct}=="c08b", MODE="0660", GROUP="input"
```

Replace `idVendor` / `idProduct` with your device's values (found via `lsusb`).

Reload rules:

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

---

## Systemd Service

After `sudo make install`:

```bash
# Enable and start
sudo systemctl enable --now PointerForce

# Check status
sudo systemctl status PointerForce

# View logs
sudo journalctl -u PointerForce -f

# Stop
sudo systemctl stop PointerForce
```

The service runs as root by default. To run as a non-root user, edit `/etc/systemd/system/PointerForce.service` and set `User=yourname` with `Group=input`.

---

## Runtime Reload

Send SIGHUP to reload config without a full restart:

```bash
scripts/reload.sh
# or manually:
kill -HUP $(cat /tmp/PointerForce.pid)
```

In Phase 1, SIGHUP triggers a graceful shutdown so the service manager can restart the process with fresh config. Full in-process hot-reload is planned for Phase 2.

---

## Logging & Debugging

In foreground/debug mode (`-f`), all output goes to stdout.

In daemon mode, output is appended to `logs/PointerForce.log` (or `/var/log/PointerForce.log` when installed as a service).

```bash
tail -f logs/PointerForce.log
```

### Useful diagnostic tools

| Tool       | Purpose |
|------------|---------|
| `evtest`   | Identify devices, inspect raw events, discover key codes |
| `strace`   | Trace syscalls — diagnose open/ioctl/grab failures |
| `lsusb`    | Find USB vendor/product IDs for udev rules |
| `udevadm monitor` | Watch device hotplug events in real time |

---

## Edge Cases & Error Handling

| Situation | Behaviour |
|-----------|-----------|
| Device path not found | Exits with error at startup |
| Invalid JSON config | Exits with parse error message |
| Unknown key name in bindings | Logs warning, skips that binding, continues |
| Device disconnected at runtime | EIO detected, loop exits cleanly, PID removed |
| Command execution failure | fork/exec errors logged; loop continues |
| Duplicate instance | Detects live PID file, exits with message |
| SIGTERM / SIGINT | Sets g_running=false, loop exits, PID file removed |
| Exclusive grab failure | Logs warning, continues without grab (non-fatal) |
| Empty bindings | Logs warning, starts anyway (useful for testing device detection) |

---

## Project Structure

```
PointerForce/
├── src/
│   ├── main.cpp        # Entry point, CLI parsing, init flow
│   ├── config.cpp      # JSON config loader and validator
│   ├── device.cpp      # libevdev device open/grab/close
│   ├── event.cpp       # EV_KEY event loop
│   ├── mapper.cpp      # Key name <-> code tables, binding lookup
│   ├── executor.cpp    # Non-blocking fork/exec command runner
│   └── daemon.cpp      # Daemonise, PID file, signal handlers
├── include/
│   ├── common.hpp      # Shared structs (Config, InputEvent), globals
│   ├── config.hpp
│   ├── device.hpp
│   ├── event.hpp
│   ├── mapper.hpp
│   ├── executor.hpp
│   └── daemon.hpp
├── config/
│   └── pointerforce.json   # Example configuration
├── scripts/
│   └── reload.sh           # Send SIGHUP to running daemon
├── logs/
│   └── pointerforce.log    # Runtime log output
├── PointerForce.service    # systemd unit file
├── Makefile
└── README.md
```

---

## Phase 2 Roadmap

Phase 1 covers userspace event listening. Phase 2 moves deeper:

- **Kernel input handler** — eBPF-based interception at the kernel level
- **Event suppression** — swallow events before they reach other applications
- **uinput injection** — synthesise and remap events at the virtual device level
- **Full hot-reload** — in-process config reload without daemon restart
- **Combo/sequence bindings** — chords, tap-vs-hold, double-tap
- **Per-application profiles** — activate binding sets based on the focused window

---

## License

MIT. See `LICENSE` for details.
