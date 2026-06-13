#!/usr/bin/env bash

set -euo pipefail

APP_DIR="${1:-/home/fmsh/fpai_demo}"
APP_NAME="${2:-sdicamera+yolov5+hdmi}"
CONFIG_PATH="${3:-configs/ZG/sdicamera+yolov5+hdmi.yaml}"
LOG_DIR="${4:-/tmp/aim_follow_smoke}"
RUN_SECONDS="${RUN_SECONDS:-20}"

mkdir -p "${LOG_DIR}"

cd "${APP_DIR}"

echo "[1/5] Check executable and config"
test -x "./${APP_NAME}" || {
    echo "Executable not found or not executable: ${APP_DIR}/${APP_NAME}" >&2
    exit 1
}
test -f "${CONFIG_PATH}" || {
    echo "Config not found: ${APP_DIR}/${CONFIG_PATH}" >&2
    exit 1
}

echo "[2/5] Check CAN interface"
if ip link show can0 >/dev/null 2>&1; then
    ip -details link show can0 | tee "${LOG_DIR}/can0_status_before.txt"
else
    echo "can0 not found" | tee "${LOG_DIR}/can0_status_before.txt"
fi

echo "[3/5] Start CAN capture if candump exists"
CAN_PID=""
if command -v candump >/dev/null 2>&1; then
    candump can0 > "${LOG_DIR}/candump.log" 2>&1 &
    CAN_PID="$!"
else
    echo "candump not found; skip CAN capture" > "${LOG_DIR}/candump.log"
fi

echo "[4/5] Run application for ${RUN_SECONDS}s"
set +e
timeout "${RUN_SECONDS}" "./${APP_NAME}" "${CONFIG_PATH}" > "${LOG_DIR}/app.log" 2>&1
APP_RET="$?"
set -e

if [[ -n "${CAN_PID}" ]]; then
    kill "${CAN_PID}" >/dev/null 2>&1 || true
fi

echo "[5/5] Summarize key logs"
{
    echo "app_ret=${APP_RET}"
    echo
    echo "AIM FOLLOW CONFIG lines:"
    grep -n "\[AIM FOLLOW CONFIG\]" "${LOG_DIR}/app.log" | tail -20 || true
    echo
    echo "AIM FOLLOW lines:"
    grep -n "\[AIM FOLLOW\]" "${LOG_DIR}/app.log" | tail -20 || true
    echo
    echo "DISTANCE DEBUG lines:"
    grep -n "\[DISTANCE DEBUG\]" "${LOG_DIR}/app.log" | tail -20 || true
    echo
    echo "CAN lines:"
    tail -50 "${LOG_DIR}/candump.log" || true
} | tee "${LOG_DIR}/summary.txt"

echo "Smoke test logs: ${LOG_DIR}"
