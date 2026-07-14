#!/bin/sh
set -eu

REMOTE_DIR="${REMOTE_DIR:-/home/fmsh/plin_pHdmi/examples/codex/plin_main_current}"
LOG_DIR="$REMOTE_DIR/logs"
VISION_LOG="$LOG_DIR/plin_live.log"
BRIDGE_PID_FILE="$LOG_DIR/safe_tracking_bridge.pid"
BRIDGE_LOG="$LOG_DIR/safe_tracking_bridge.log"
SAFE_PID_FILE="$LOG_DIR/safe_can_control_session.pid"
SAFE_LOG="$LOG_DIR/safe_can_control_session.log"
SAFE_SOCKET=/tmp/plin_safe_can_control.sock
SAFE_STATUS=/tmp/plin_safe_can_status.json

mkdir -p "$LOG_DIR"

if [ ! -f "$VISION_LOG" ]; then
    echo "[AUTO ERROR] vision log is missing: $VISION_LOG" >&2
    exit 1
fi

if [ -f "$BRIDGE_PID_FILE" ]; then
    BRIDGE_PID=$(cat "$BRIDGE_PID_FILE" 2>/dev/null || true)
    if [ -n "$BRIDGE_PID" ] && kill -0 "$BRIDGE_PID" 2>/dev/null; then
        echo "[AUTO READY] tracking bridge already running pid=$BRIDGE_PID"
        exit 0
    fi
    rm -f "$BRIDGE_PID_FILE"
fi

if ! python3 - "$SAFE_SOCKET" "$SAFE_STATUS" <<'PY'
import json
import os
import sys
import time

socket_path, status_path = sys.argv[1:]
ready = os.path.exists(socket_path) and os.path.exists(status_path)
if ready:
    ready = time.time() - os.path.getmtime(status_path) <= 2.0
if ready:
    with open(status_path, "r", encoding="ascii") as source:
        ready = json.load(source).get("mode") == 0xAA
raise SystemExit(0 if ready else 1)
PY
then
    if pgrep -f '^python3 .*/safe_can_control_session\.py($| )' >/dev/null 2>&1; then
        echo "[AUTO ERROR] CAN session process exists but is not ready" >&2
        exit 1
    fi
    nohup python3 "$REMOTE_DIR/tools/safe_can_control_session.py" \
        > "$SAFE_LOG" 2>&1 &
    SAFE_PID=$!
    echo "$SAFE_PID" > "$SAFE_PID_FILE"

    READY=0
    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
        sleep 0.2
        if [ -S "$SAFE_SOCKET" ] && [ -f "$SAFE_STATUS" ] && \
           grep -q '"mode": 170' "$SAFE_STATUS" && \
           kill -0 "$SAFE_PID" 2>/dev/null; then
            READY=1
            break
        fi
    done
    if [ "$READY" -ne 1 ]; then
        echo "[AUTO ERROR] CAN session failed to enter mode 0xAA" >&2
        tail -20 "$SAFE_LOG" >&2 || true
        exit 1
    fi
fi

nohup python3 "$REMOTE_DIR/tools/safe_tracking_bridge.py" \
    --log "$VISION_LOG" \
    --arm \
    --track-rpm-limit 50 \
    --search-rpm-limit 40 \
    --search-confirm 0.35 \
    > "$BRIDGE_LOG" 2>&1 &
BRIDGE_PID=$!
echo "$BRIDGE_PID" > "$BRIDGE_PID_FILE"
sleep 0.4

if ! kill -0 "$BRIDGE_PID" 2>/dev/null; then
    echo "[AUTO ERROR] tracking bridge exited during startup" >&2
    tail -20 "$BRIDGE_LOG" >&2 || true
    exit 1
fi

echo "[AUTO READY] tracking=armed pid=$BRIDGE_PID gimbal=disabled"
echo "[AUTO LIMITS] forward=45rpm steer=50rpm search=40rpm target_distance=1.00m"
