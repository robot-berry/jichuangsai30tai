#!/bin/sh
set -eu

SERVICE_NAME=plin-autonomous-tracking.service
REMOTE_DIR="${REMOTE_DIR:-/home/fmsh/plin_pHdmi/examples/codex/plin_main_current}"

if [ "$(id -u)" -ne 0 ]; then
    echo "[SERVICE ERROR] Run as root." >&2
    exit 1
fi

systemctl stop "$SERVICE_NAME" >/dev/null 2>&1 || true
systemctl disable "$SERVICE_NAME" >/dev/null 2>&1 || true
rm -f "/etc/systemd/system/$SERVICE_NAME"
systemctl daemon-reload
systemctl reset-failed "$SERVICE_NAME" >/dev/null 2>&1 || true
"$REMOTE_DIR/stop_all.sh" >/dev/null 2>&1 || true
echo "[SERVICE REMOVED] name=$SERVICE_NAME motors=0/0 can0=down"
