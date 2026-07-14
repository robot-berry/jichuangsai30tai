#!/bin/sh
set -eu

REMOTE_DIR="${REMOTE_DIR:-/home/fmsh/plin_pHdmi/examples/codex/plin_main_current}"
BOOT_DELAY="${BOOT_DELAY:-10}"
START_RETRIES="${START_RETRIES:-6}"
CHECK_PERIOD="${CHECK_PERIOD:-2}"
LOG_DIR="$REMOTE_DIR/logs"
STATUS_PATH=/tmp/plin_safe_can_status.json

cleanup() {
    trap - EXIT INT TERM HUP
    if [ -x "$REMOTE_DIR/stop_all.sh" ]; then
        "$REMOTE_DIR/stop_all.sh" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT INT TERM HUP

case "$START_RETRIES" in
    ''|*[!0-9]*|0)
        echo "[BOOT ERROR] START_RETRIES must be a positive integer." >&2
        exit 2
        ;;
esac

mkdir -p "$LOG_DIR"
echo "[BOOT WAIT] delay=${BOOT_DELAY}s remote_dir=$REMOTE_DIR"
sleep "$BOOT_DELAY"

ATTEMPT=1
while :; do
    "$REMOTE_DIR/stop_all.sh" >/dev/null 2>&1 || true
    if RUN_SECONDS=0 "$REMOTE_DIR/start_vision_dryrun.sh"; then
        break
    fi
    if [ "$ATTEMPT" -ge "$START_RETRIES" ]; then
        echo "[BOOT ERROR] vision failed after $ATTEMPT attempts." >&2
        exit 1
    fi
    echo "[BOOT RETRY] vision attempt=$ATTEMPT" >&2
    ATTEMPT=$((ATTEMPT + 1))
    sleep 5
done

sleep 5
if ! REMOTE_DIR="$REMOTE_DIR" "$REMOTE_DIR/tools/start_autonomous_tracking.sh"; then
    echo "[BOOT ERROR] autonomous tracking failed to start." >&2
    exit 1
fi

echo "[BOOT READY] ByteTrack chassis tracking is supervised."
while sleep "$CHECK_PERIOD"; do
    if ! python3 - "$LOG_DIR" "$STATUS_PATH" <<'PY'
import json
import os
import sys
import time

log_dir, status_path = sys.argv[1:]
for name in ("app.pid", "safe_can_control_session.pid", "safe_tracking_bridge.pid"):
    with open(os.path.join(log_dir, name), "r", encoding="ascii") as source:
        pid = int(source.read().strip())
    os.kill(pid, 0)

vision_log = os.path.join(log_dir, "plin_live.log")
if time.time() - os.path.getmtime(vision_log) > 5.0:
    raise SystemExit("vision log is stale")
if time.time() - os.path.getmtime(status_path) > 3.0:
    raise SystemExit("CAN status is stale")
with open(status_path, "r", encoding="ascii") as source:
    status = json.load(source)
if "mode" not in status or "command_motor1" not in status or "command_motor2" not in status:
    raise SystemExit("CAN status is incomplete")
PY
    then
        echo "[BOOT ERROR] supervised process or heartbeat failed." >&2
        exit 1
    fi
done
