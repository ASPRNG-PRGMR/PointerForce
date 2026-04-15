#!/usr/bin/env bash
# reload.sh – send SIGHUP to the running PointerForce daemon
# This will cause the daemon to re-read its config on the next loop iteration.
# (Phase 1: triggers clean restart via signal; full hot-reload in Phase 2)

PID_FILE="/tmp/pointerforce.pid"

if [[ ! -f "$PID_FILE" ]]; then
    echo "pointerforce does not appear to be running (no PID file)."
    exit 1
fi

PID=$(cat "$PID_FILE")

if ! kill -0 "$PID" 2>/dev/null; then
    echo "Stale PID file ($PID_FILE). Process $PID not found."
    exit 1
fi

kill -HUP "$PID" && echo "Sent SIGHUP to pointerforce (PID $PID)."
