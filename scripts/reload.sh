#!/usr/bin/env bash
# reload.sh – send SIGHUP to the running PointerForce daemon.
# Phase 2: SIGHUP triggers a full in-process config reload and mapper reinit
# without stopping the event loop or dropping any device connections.

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

kill -HUP "$PID" && echo "Sent SIGHUP to pointerforce (PID $PID). Config reloading in-process."
