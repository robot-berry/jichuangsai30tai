#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

APP_PATH="${APP_PATH:-./build/ZG/sdicamera+yolov5+hdmi}"
CONFIG_PATH="${1:-configs/ZG/sdicamera+yolov5+hdmi.yaml}"

if [ ! -x "$APP_PATH" ]; then
    echo "Executable not found or not executable: $APP_PATH" >&2
    exit 1
fi

if [ ! -f "$CONFIG_PATH" ]; then
    echo "Config not found: $CONFIG_PATH" >&2
    exit 1
fi

# The 3.33.1 board runtime lives in the system library directories.
# Keep these first so older copied deps/so libraries cannot override them.
SYSTEM_LIBS="/usr/lib/aarch64-linux-gnu:/lib/aarch64-linux-gnu"
if [ -n "${LD_LIBRARY_PATH:-}" ]; then
    export LD_LIBRARY_PATH="${SYSTEM_LIBS}:${LD_LIBRARY_PATH}"
else
    export LD_LIBRARY_PATH="${SYSTEM_LIBS}"
fi

export AIM_FOLLOW_CAN_DRYRUN="${AIM_FOLLOW_CAN_DRYRUN:-1}"

exec "$APP_PATH" "$CONFIG_PATH"
