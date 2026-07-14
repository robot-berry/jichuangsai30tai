#!/bin/sh
set -eu

if [ "${ARM_REAL_CAN:-NO}" != "YES" ]; then
    echo "[SAFETY] Set ARM_REAL_CAN=YES only after the movement area is clear." >&2
    exit 2
fi
if [ "${AREA_CLEAR:-NO}" != "YES" ]; then
    echo "[SAFETY] Set AREA_CLEAR=YES only after checking the complete movement area." >&2
    exit 2
fi

REMOTE_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
LOG_DIR="$REMOTE_DIR/logs"
RUN_SECONDS="${RUN_SECONDS:-5}"
VISION_SETTLE_SECONDS="${VISION_SETTLE_SECONDS:-5}"
SAFE_SOCKET=/tmp/plin_safe_can_control.sock
SAFE_STATUS=/tmp/plin_safe_can_status.json
SAFE_LOG="$LOG_DIR/chassis_tracking_test_session.log"
BRIDGE_LOG="$LOG_DIR/chassis_tracking_test_bridge.log"
SAFE_PID=""

if ! awk -v duration="$RUN_SECONDS" 'BEGIN {
    exit !(duration ~ /^[0-9]+([.][0-9]+)?$/ && duration > 0 && duration <= 30)
}'; then
    echo "[SAFETY] RUN_SECONDS must be greater than 0 and no more than 30." >&2
    exit 2
fi

cleanup() {
    if [ -S "$SAFE_SOCKET" ]; then
        python3 "$REMOTE_DIR/tools/safe_can_control_client.py" \
            --motor1 0 --motor2 0 --duration 0.15 >/dev/null 2>&1 || true
    fi
    if [ -n "$SAFE_PID" ]; then
        kill "$SAFE_PID" 2>/dev/null || true
        wait "$SAFE_PID" 2>/dev/null || true
    fi
    rm -f "$LOG_DIR/chassis_tracking_test_safe.pid"
    ip link set can0 down >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

mkdir -p "$LOG_DIR"
"$REMOTE_DIR/stop_all.sh" >/dev/null 2>&1 || true
DRYRUN_GIMBAL_ENABLE=0 \
DRYRUN_CHASSIS_ENABLE=1 \
DRYRUN_SEARCH_ENABLE=0 \
RUN_SECONDS=86400 \
    "$REMOTE_DIR/start_vision_dryrun.sh"
sleep "$VISION_SETTLE_SECONDS"

if ! grep -a -q '\[AIM FOLLOW\]' "$LOG_DIR/plin_live.log"; then
    echo "[TRACKING ERROR] ByteTrack has no locked target; CAN remains disabled." >&2
    exit 1
fi

rm -f "$SAFE_SOCKET" "$SAFE_STATUS" "$SAFE_LOG" "$BRIDGE_LOG"
python3 "$REMOTE_DIR/tools/safe_can_control_session.py" \
    --max-rpm 60 --max-pulse 0.2 >"$SAFE_LOG" 2>&1 &
SAFE_PID=$!
echo "$SAFE_PID" > "$LOG_DIR/chassis_tracking_test_safe.pid"

READY=0
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30; do
    if [ -S "$SAFE_SOCKET" ] && [ -f "$SAFE_STATUS" ] && \
       grep -q '"mode": 170' "$SAFE_STATUS"; then
        READY=1
        break
    fi
    kill -0 "$SAFE_PID" 2>/dev/null || break
    sleep 0.1
done
if [ "$READY" -ne 1 ]; then
    echo "[TRACKING ERROR] chassis did not enter CAN mode 0xAA." >&2
    cat "$SAFE_LOG" >&2 || true
    exit 1
fi

python3 "$REMOTE_DIR/tools/safe_tracking_bridge.py" \
    --log "$LOG_DIR/plin_live.log" \
    --arm \
    --track-rpm-limit 60 \
    --search-rpm-limit 40 \
    --search-confirm 0.35 \
    --max-runtime "$RUN_SECONDS" \
    >"$BRIDGE_LOG" 2>&1
sleep 0.3

grep -a '\[DISTANCE DEBUG\]' "$LOG_DIR/plin_live.log" | tail -1 || true
grep -a '\[AIM FOLLOW\]' "$LOG_DIR/plin_live.log" | tail -1 || true
cat "$SAFE_STATUS" 2>/dev/null || true
cat "$BRIDGE_LOG"
echo "[TRACKING TEST DONE] duration=${RUN_SECONDS}s; motors=0/0; can0 will be down"
