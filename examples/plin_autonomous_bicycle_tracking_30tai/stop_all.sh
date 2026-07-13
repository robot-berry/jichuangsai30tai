#!/bin/sh
set -eu

REMOTE_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
LOG_DIR="$REMOTE_DIR/logs"

REMOTE_DIR="$REMOTE_DIR" "$REMOTE_DIR/tools/stop_autonomous_tracking.sh" >/dev/null 2>&1 || true
sleep 1

for PID_FILE in "$LOG_DIR/app.pid" "$LOG_DIR/keepalive.pid"; do
    if [ -f "$PID_FILE" ]; then
        PID=$(cat "$PID_FILE" 2>/dev/null || true)
        if [ -n "$PID" ]; then
            pkill -P "$PID" 2>/dev/null || true
            kill "$PID" 2>/dev/null || true
        fi
    fi
done

for PID in $(pgrep -f '^python3 tools/stream_plin_hdmi_udma.py ' 2>/dev/null || true); do
    kill "$PID" 2>/dev/null || true
done

SAFE_PID=$(cat "$LOG_DIR/safe_can_control_session.pid" 2>/dev/null || true)
if [ -z "$SAFE_PID" ]; then
    SAFE_PID=$(pgrep -f '^python3 .*/safe_can_control_session\.py($| )' 2>/dev/null | head -1 || true)
fi
if [ -n "$SAFE_PID" ] && kill -0 "$SAFE_PID" 2>/dev/null; then
    kill "$SAFE_PID"
    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
        kill -0 "$SAFE_PID" 2>/dev/null || break
        sleep 0.2
    done
fi

ip link set can0 down 2>/dev/null || true
echo "[STOPPED] vision=off tracking=off motors=0/0 can0=down"
