#!/usr/bin/env bash

set -euo pipefail

APP_DIR="${1:-/home/fmsh/fpai_demo}"
APP_NAME="${2:-sdicamera+yolov5+hdmi}"
CONFIG_PATH="${3:-configs/ZG/sdicamera+yolov5+hdmi.yaml}"
LOG_DIR="${4:-/tmp/aim_follow_smoke}"
RUN_SECONDS="${RUN_SECONDS:-20}"
AIM_FOLLOW_CAN_DRYRUN="${AIM_FOLLOW_CAN_DRYRUN:-1}"

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

echo "[2/5] CAN dry-run mode"
echo "AIM_FOLLOW_CAN_DRYRUN=${AIM_FOLLOW_CAN_DRYRUN}" | tee "${LOG_DIR}/can_dryrun_status.txt"
echo "CAN capture skipped in vision/algorithm smoke test." > "${LOG_DIR}/candump.log"

echo "[3/5] Run application for ${RUN_SECONDS}s"
set +e
AIM_FOLLOW_CAN_DRYRUN="${AIM_FOLLOW_CAN_DRYRUN}" timeout "${RUN_SECONDS}" "./${APP_NAME}" "${CONFIG_PATH}" > "${LOG_DIR}/app.log" 2>&1
APP_RET="$?"
set -e

echo "[4/5] Summarize key logs"
{
    echo "app_ret=${APP_RET}"
    echo "AIM_FOLLOW_CAN_DRYRUN=${AIM_FOLLOW_CAN_DRYRUN}"
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
    echo "CAN DRYRUN lines:"
    grep -n "\[CAN DRYRUN\]\|DRYRUN id=0x" "${LOG_DIR}/app.log" | tail -20 || true
} | tee "${LOG_DIR}/summary.txt"

echo "Smoke test logs: ${LOG_DIR}"
