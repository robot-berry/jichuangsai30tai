#!/bin/sh
set -eu

REMOTE_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
LOG_DIR="$REMOTE_DIR/logs"
CONFIG="$REMOTE_DIR/configs/ZG/sdicamera+yolov5+hdmi_runtime.yaml"
RUN_SECONDS="${RUN_SECONDS:-86400}"
SYNTHETIC_TARGET="${SYNTHETIC_TARGET:-0}"
DRYRUN_GIMBAL_ENABLE="${DRYRUN_GIMBAL_ENABLE:-0}"
DRYRUN_CHASSIS_ENABLE="${DRYRUN_CHASSIS_ENABLE:-1}"
DRYRUN_SEARCH_ENABLE="${DRYRUN_SEARCH_ENABLE:-0}"
DRYRUN_LASER_AIM_ENABLE="${DRYRUN_LASER_AIM_ENABLE:-0}"

case "$DRYRUN_GIMBAL_ENABLE:$DRYRUN_CHASSIS_ENABLE:$DRYRUN_SEARCH_ENABLE:$DRYRUN_LASER_AIM_ENABLE" in
    [01]:[01]:[01]:[01]) ;;
    *)
        echo "[VISION ERROR] DRYRUN control switches must be 0 or 1." >&2
        exit 2
        ;;
esac

mkdir -p "$LOG_DIR"
REMOTE_DIR="$REMOTE_DIR" "$REMOTE_DIR/tools/stop_autonomous_tracking.sh" >/dev/null 2>&1 || true

for PID_FILE in "$LOG_DIR/app.pid" "$LOG_DIR/keepalive.pid"; do
    if [ -f "$PID_FILE" ]; then
        PID=$(cat "$PID_FILE" 2>/dev/null || true)
        if [ -n "$PID" ]; then
            pkill -P "$PID" 2>/dev/null || true
            kill "$PID" 2>/dev/null || true
        fi
    fi
done
sleep 1
ip link set can0 down >/dev/null 2>&1 || true

cp "$REMOTE_DIR/configs/ZG/sdicamera+yolov5+hdmi.yaml" "$CONFIG"
rm -f "$LOG_DIR/plin_live.log" "$LOG_DIR/app.pid" \
    "$LOG_DIR/keepalive.pid" "$LOG_DIR/plin_stdin.fifo"
mkfifo "$LOG_DIR/plin_stdin.fifo"

cd "$REMOTE_DIR"
(cat "$LOG_DIR/plin_stdin.fifo" | timeout "$RUN_SECONDS" env \
    AIM_FOLLOW_CAN_DRYRUN=1 \
    AIM_FOLLOW_SETUP_CAN=0 \
    AIM_FOLLOW_SYNTHETIC_TARGET="$SYNTHETIC_TARGET" \
    AIM_FOLLOW_BYTETRACK_ENABLE=1 \
    AIM_FOLLOW_LASER_AIM_ENABLE="$DRYRUN_LASER_AIM_ENABLE" \
    AIM_FOLLOW_LASER_MOTION_ENABLE=1 \
    AIM_FOLLOW_LASER_MIN_YAW=100 \
    AIM_FOLLOW_LASER_MAX_YAW=165 \
    AIM_FOLLOW_LASER_COARSE_YAW_ENABLE=0 \
    AIM_FOLLOW_LASER_FINE_YAW_ENABLE=1 \
    AIM_FOLLOW_LASER_DEBUG_VIEW=0 \
    AIM_FOLLOW_LASER_DEBUG_GAIN=4 \
    AIM_FOLLOW_DISTANCE_FOCAL_PX=600 \
    AIM_FOLLOW_DISTANCE_STABILITY_DEADBAND_M=0.01 \
    AIM_FOLLOW_GIMBAL_ENABLE="$DRYRUN_GIMBAL_ENABLE" \
    AIM_FOLLOW_CHASSIS_ENABLE="$DRYRUN_CHASSIS_ENABLE" \
    AIM_FOLLOW_DISTANCE_ENABLE=1 \
    AIM_FOLLOW_TARGET_DISTANCE_M=1.0 \
    AIM_FOLLOW_DISTANCE_DEADBAND_M=0.03 \
    AIM_FOLLOW_DISTANCE_RESUME_DEADBAND_M=0.08 \
    AIM_FOLLOW_MIN_FOLLOW_RPM=40 \
    AIM_FOLLOW_MAX_FOLLOW_RPM=40 \
    AIM_FOLLOW_CHASSIS_STEER_ENABLE=1 \
    AIM_FOLLOW_STEER_DEADZONE_NORM=0.08 \
    AIM_FOLLOW_STEER_KP_RPM=45 \
    AIM_FOLLOW_MIN_STEER_RPM=40 \
    AIM_FOLLOW_MAX_STEER_RPM=40 \
    AIM_FOLLOW_MOTOR1_STEER_SIGN=1 \
    AIM_FOLLOW_MOTOR2_STEER_SIGN=-1 \
    AIM_FOLLOW_MOTOR2_FORWARD_SIGN=1 \
    AIM_FOLLOW_SEARCH_ENABLE="$DRYRUN_SEARCH_ENABLE" \
    AIM_FOLLOW_SEARCH_RPM=40 \
    AIM_FOLLOW_SEARCH_SWEEP_FRAMES=60 \
    AIM_FOLLOW_DEFAULT_SEARCH_DIRECTION=-1 \
    AIM_FOLLOW_YAW_KP=0 \
    AIM_FOLLOW_YAW_KD=0 \
    AIM_FOLLOW_PITCH_KP=0 \
    AIM_FOLLOW_PITCH_KD=0 \
    stdbuf -oL -eL "$REMOTE_DIR/run_30tai_3331.sh" "$CONFIG" \
    > "$LOG_DIR/plin_live.log" 2>&1) >/dev/null 2>&1 &
APP_PID=$!

(
    sleep 1
    exec 9>"$LOG_DIR/plin_stdin.fifo"
    while kill -0 "$APP_PID" 2>/dev/null; do
        sleep 1
    done
) >/dev/null 2>&1 &
KEEP_PID=$!

echo "$APP_PID" > "$LOG_DIR/app.pid"
echo "$KEEP_PID" > "$LOG_DIR/keepalive.pid"
sleep 2

if ! kill -0 "$APP_PID" 2>/dev/null; then
    echo "[VISION ERROR] application failed to start" >&2
    tail -40 "$LOG_DIR/plin_live.log" >&2 || true
    exit 1
fi

echo "[VISION READY] pid=$APP_PID can_dryrun=1 gimbal=$DRYRUN_GIMBAL_ENABLE chassis=$DRYRUN_CHASSIS_ENABLE search=$DRYRUN_SEARCH_ENABLE laser=$DRYRUN_LASER_AIM_ENABLE synthetic=$SYNTHETIC_TARGET"
