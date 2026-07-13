#!/bin/sh
set -eu

REMOTE_DIR="${REMOTE_DIR:-/home/fmsh/plin_pHdmi/examples/codex/plin_main_current}"
PID_FILE="$REMOTE_DIR/logs/safe_tracking_bridge.pid"
SAFE_SOCKET=/tmp/plin_safe_can_control.sock

if [ -f "$PID_FILE" ]; then
    PID=$(cat "$PID_FILE" 2>/dev/null || true)
    if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
        kill "$PID"
        for _ in 1 2 3 4 5 6 7 8 9 10; do
            kill -0 "$PID" 2>/dev/null || break
            sleep 0.1
        done
    fi
    rm -f "$PID_FILE"
fi

if [ -S "$SAFE_SOCKET" ]; then
    python3 "$REMOTE_DIR/tools/safe_can_control_client.py" \
        --motor1 0 --motor2 0 --duration 0.15
fi

echo "[AUTO STOP] tracking bridge stopped; motors=0/0"
