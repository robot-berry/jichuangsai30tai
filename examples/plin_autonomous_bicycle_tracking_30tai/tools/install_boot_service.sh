#!/bin/sh
set -eu

SERVICE_NAME=plin-autonomous-tracking.service
REMOTE_DIR="${REMOTE_DIR:-/home/fmsh/plin_pHdmi/examples/codex/plin_main_current}"
START_NOW="${START_NOW:-1}"
UNIT_SOURCE="$REMOTE_DIR/systemd/$SERVICE_NAME"
UNIT_TARGET="/etc/systemd/system/$SERVICE_NAME"
TEMP_UNIT=$(mktemp)

cleanup() {
    rm -f "$TEMP_UNIT"
}
trap cleanup EXIT INT TERM HUP

if [ "$(id -u)" -ne 0 ]; then
    echo "[SERVICE ERROR] Run as root." >&2
    exit 1
fi
if [ ! -f "$UNIT_SOURCE" ]; then
    echo "[SERVICE ERROR] Missing unit template: $UNIT_SOURCE" >&2
    exit 1
fi
case "$START_NOW" in
    0|1) ;;
    *)
        echo "[SERVICE ERROR] START_NOW must be 0 or 1." >&2
        exit 2
        ;;
esac

sed "s|@REMOTE_DIR@|$REMOTE_DIR|g" "$UNIT_SOURCE" > "$TEMP_UNIT"
install -m 0644 "$TEMP_UNIT" "$UNIT_TARGET"
systemctl daemon-reload
systemctl enable "$SERVICE_NAME" >/dev/null

if [ "$START_NOW" -eq 1 ]; then
    systemctl restart "$SERVICE_NAME"
fi

echo "[SERVICE READY] name=$SERVICE_NAME enabled=1 start_now=$START_NOW"
