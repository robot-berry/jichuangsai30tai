#!/bin/sh
set -eu

if [ "${ARM_REAL_CAN:-NO}" != "YES" ]; then
    echo "[SAFETY] Set ARM_REAL_CAN=YES only after the vehicle is lifted or the area is clear." >&2
    exit 2
fi
if [ "${AREA_CLEAR:-NO}" != "YES" ]; then
    echo "[SAFETY] Set AREA_CLEAR=YES only after checking the complete movement area." >&2
    exit 2
fi

REMOTE_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
LOG_DIR="$REMOTE_DIR/logs"
CONFIG="$REMOTE_DIR/configs/ZG/sdicamera+yolov5+hdmi_runtime.yaml"
RUN_SECONDS="${RUN_SECONDS:-120}"
ENABLE_CHASSIS="${ENABLE_CHASSIS:-1}"
ENABLE_GIMBAL="${ENABLE_GIMBAL:-0}"
ENABLE_LASER="${ENABLE_LASER:-0}"

case "$ENABLE_CHASSIS:$ENABLE_GIMBAL:$ENABLE_LASER" in
    [01]:[01]:[01]) ;;
    *)
        echo "[SAFETY] ENABLE_CHASSIS, ENABLE_GIMBAL and ENABLE_LASER must be 0 or 1." >&2
        exit 2
        ;;
esac
if [ "$ENABLE_LASER" = "1" ] && [ "$ENABLE_GIMBAL" != "1" ]; then
    echo "[SAFETY] ENABLE_LASER=1 requires ENABLE_GIMBAL=1." >&2
    exit 2
fi
if [ "$ENABLE_LASER" = "1" ] && [ "${IR_READY:-NO}" != "YES" ]; then
    echo "[SAFETY] ENABLE_LASER=1 requires IR_READY=YES after the emitter is on." >&2
    exit 2
fi

mkdir -p "$LOG_DIR"
"$REMOTE_DIR/stop_all.sh" >/dev/null 2>&1 || true
cp "$REMOTE_DIR/configs/ZG/sdicamera+yolov5+hdmi.yaml" "$CONFIG"
rm -f "$LOG_DIR/plin_live.log" "$LOG_DIR/app.pid" \
    "$LOG_DIR/keepalive.pid" "$LOG_DIR/plin_stdin.fifo"
mkfifo "$LOG_DIR/plin_stdin.fifo"

cd "$REMOTE_DIR"
(cat "$LOG_DIR/plin_stdin.fifo" | timeout "$RUN_SECONDS" env \
    AIM_FOLLOW_CAN_DRYRUN=0 \
    AIM_FOLLOW_SETUP_CAN=1 \
    AIM_FOLLOW_CAN_BITRATE=250000 \
    AIM_FOLLOW_BYTETRACK_ENABLE=1 \
    AIM_FOLLOW_LASER_AIM_ENABLE="$ENABLE_LASER" \
    AIM_FOLLOW_LASER_MOTION_ENABLE=1 \
    AIM_FOLLOW_DISTANCE_FOCAL_PX=544 \
    AIM_FOLLOW_GIMBAL_ENABLE="$ENABLE_GIMBAL" \
    AIM_FOLLOW_GIMBAL_REPEAT=1 \
    AIM_FOLLOW_CHASSIS_ENABLE="$ENABLE_CHASSIS" \
    AIM_FOLLOW_DISTANCE_ENABLE=1 \
    AIM_FOLLOW_TARGET_DISTANCE_M=1.0 \
    AIM_FOLLOW_DISTANCE_DEADBAND_M=0.05 \
    AIM_FOLLOW_MIN_FOLLOW_RPM=35 \
    AIM_FOLLOW_MAX_FOLLOW_RPM=35 \
    AIM_FOLLOW_CHASSIS_STEER_ENABLE="$ENABLE_CHASSIS" \
    AIM_FOLLOW_STEER_DEADZONE_NORM=0.08 \
    AIM_FOLLOW_STEER_KP_RPM=45 \
    AIM_FOLLOW_MIN_STEER_RPM=35 \
    AIM_FOLLOW_MAX_STEER_RPM=35 \
    AIM_FOLLOW_MOTOR1_STEER_SIGN=1 \
    AIM_FOLLOW_MOTOR2_STEER_SIGN=-1 \
    AIM_FOLLOW_MOTOR1_FORWARD_SIGN=1 \
    AIM_FOLLOW_MOTOR2_FORWARD_SIGN=1 \
    AIM_FOLLOW_SEARCH_ENABLE=0 \
    AIM_FOLLOW_YAW_KP=0 \
    AIM_FOLLOW_YAW_KD=0 \
    AIM_FOLLOW_PITCH_KP=0 \
    AIM_FOLLOW_PITCH_KD=0 \
    AIM_FOLLOW_LASER_MIN_YAW=100 \
    AIM_FOLLOW_LASER_MAX_YAW=165 \
    AIM_FOLLOW_LASER_MIN_PITCH=120 \
    AIM_FOLLOW_LASER_MAX_PITCH=180 \
    AIM_FOLLOW_LASER_COARSE_YAW_STEP=5 \
    AIM_FOLLOW_LASER_COARSE_YAW_ENABLE=0 \
    AIM_FOLLOW_LASER_COARSE_PITCH_STEP=5 \
    AIM_FOLLOW_LASER_FINE_YAW_ENABLE=1 \
    AIM_FOLLOW_LASER_FINE_MAX_STEP=2 \
    stdbuf -oL -eL "$REMOTE_DIR/run_30tai_3331.sh" "$CONFIG" \
    > "$LOG_DIR/plin_live.log" 2>&1) >/dev/null 2>&1 &
APP_PID=$!

(
    sleep 1
    exec 9>"$LOG_DIR/plin_stdin.fifo"
    while kill -0 "$APP_PID" 2>/dev/null; do
        sleep 1
    done
    if command -v cansend >/dev/null 2>&1; then
        cansend can0 201#0000000064640001 >/dev/null 2>&1 || true
        sleep 0.05
        cansend can0 201#0000000064640000 >/dev/null 2>&1 || true
    fi
    ip link set can0 down >/dev/null 2>&1 || true
) >/dev/null 2>&1 &
KEEP_PID=$!

echo "$APP_PID" > "$LOG_DIR/app.pid"
echo "$KEEP_PID" > "$LOG_DIR/keepalive.pid"
sleep 2

if ! kill -0 "$APP_PID" 2>/dev/null; then
    echo "[TRACKING ERROR] application failed to start" >&2
    tail -40 "$LOG_DIR/plin_live.log" >&2 || true
    exit 1
fi

echo "[TRACKING READY] pid=$APP_PID duration=${RUN_SECONDS}s chassis=$ENABLE_CHASSIS gimbal=$ENABLE_GIMBAL laser=$ENABLE_LASER"
