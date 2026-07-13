#!/bin/sh
set -eu

if [ "${ARM_REAL_CAN:-NO}" != "YES" ] || [ "${AREA_CLEAR:-NO}" != "YES" ]; then
    echo "[SAFETY] Laser aiming requires ARM_REAL_CAN=YES and AREA_CLEAR=YES." >&2
    exit 2
fi
if [ "${IR_READY:-NO}" != "YES" ]; then
    echo "[SAFETY] Turn on the infrared emitter, then set IR_READY=YES." >&2
    exit 2
fi

REMOTE_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
RUN_SECONDS="${RUN_SECONDS:-45}"

exec env \
    ARM_REAL_CAN=YES \
    AREA_CLEAR=YES \
    IR_READY=YES \
    RUN_SECONDS="$RUN_SECONDS" \
    ENABLE_CHASSIS=0 \
    ENABLE_GIMBAL=1 \
    ENABLE_LASER=1 \
    "$REMOTE_DIR/start_tracking_test.sh"
