param(
    [string]$BoardIp = "192.168.125.171",
    [string]$User = "root",
    [string]$SshKey = "",
    [string]$RemoteDir = "/home/work/fpai_demo_app/examples/codex/fpai_demo_src",
    [string]$OriginalDir = "/home/work/fpai_demo_app/examples/1_single_input+ai/PLin+SingleNet+HDMI",
    [string]$LocalLogDir = ""
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$SshTarget = "$User@$BoardIp"
$SshOptionArgs = @("-o", "ConnectTimeout=10", "-o", "StrictHostKeyChecking=no")
if (-not [string]::IsNullOrWhiteSpace($SshKey)) {
    $SshKey = (Resolve-Path $SshKey).Path
    $SshOptionArgs += @("-i", $SshKey)
}

if ([string]::IsNullOrWhiteSpace($LocalLogDir)) {
    $LocalLogDir = Join-Path $RepoRoot "board_camera_path_diagnostics"
}

$RemoteLogDir = "/tmp/aim_follow_camera_path_diag_$(Get-Date -Format 'yyyyMMdd_HHmmss')"

Write-Host "==== 30TAI camera/PL input path diagnostic ====" -ForegroundColor Cyan
Write-Host "Board:        $SshTarget"
Write-Host "RemoteDir:    $RemoteDir"
Write-Host "OriginalDir:  $OriginalDir"
Write-Host "RemoteLogDir: $RemoteLogDir"
Write-Host "LocalLogDir:  $LocalLogDir"
if (-not [string]::IsNullOrWhiteSpace($SshKey)) {
    Write-Host "SSH key:      $SshKey"
}

$remoteScript = @"
set +e

LOG_DIR='$RemoteLogDir'
REMOTE_DIR='$RemoteDir'
ORIGINAL_DIR='$OriginalDir'

mkdir -p "`$LOG_DIR"

run_cmd() {
    name="`$1"
    shift
    {
        echo "[command] `$*"
        echo
        "`$@"
        echo
        echo "[ret] `$?"
    } > "`$LOG_DIR/`$name.txt" 2>&1
}

{
    echo "[date]"
    date
    echo
    echo "[uname]"
    uname -a
    echo
    echo "[cmdline]"
    cat /proc/cmdline 2>/dev/null || true
    echo
    echo "[mounts fpga firmware hints]"
    mount | grep -Ei 'boot|firmware|fpga|xilinx|zg|icraft' || true
    echo
    echo "[processes]"
    ps -ef | grep -Ei 'sdicamera|yolov5|fpai|camera|video|hdmi' | grep -v grep || true
} > "`$LOG_DIR/system_basic.txt" 2>&1

{
    echo "[device nodes]"
    ls -l /dev/video* /dev/media* /dev/v4l* /dev/fb* /dev/dri/* /dev/i2c* /dev/spidev* 2>/dev/null || true
    echo
    echo "[sys video4linux]"
    find /sys/class/video4linux -maxdepth 3 -type f -print -exec sh -c 'echo "---"; cat "$1" 2>/dev/null' _ {} \; 2>/dev/null || true
    echo
    echo "[sys media]"
    find /sys/class/media -maxdepth 3 -type f -print -exec sh -c 'echo "---"; cat "$1" 2>/dev/null' _ {} \; 2>/dev/null || true
} > "`$LOG_DIR/devices.txt" 2>&1

{
    echo "[loaded modules camera/video/fpga hints]"
    lsmod 2>/dev/null | grep -Ei 'video|v4l|media|cam|sdi|hdmi|xilinx|fpga|zg|icraft' || true
    echo
    echo "[interrupts camera/video/fpga hints]"
    cat /proc/interrupts 2>/dev/null | grep -Ei 'video|v4l|media|cam|sdi|hdmi|xilinx|fpga|zg|icraft|dma' || true
} > "`$LOG_DIR/driver_hints.txt" 2>&1

{
    echo "[dmesg selected]"
    dmesg | grep -Ei 'video|v4l|media|camera|cam|sdi|hdmi|xilinx|fpga|zg|icraft|dma|i2c|sensor|mipi|csi|imk|imagemake' | tail -n 300 || true
    echo
    echo "[dmesg tail]"
    dmesg | tail -n 160 || true
} > "`$LOG_DIR/dmesg_camera.txt" 2>&1

{
    echo "[available tools]"
    for tool in v4l2-ctl media-ctl ffmpeg gst-launch-1.0 yavta i2cdetect i2cdump i2cget; do
        if command -v "`$tool" >/dev/null 2>&1; then
            echo "`$tool: `$(command -v "`$tool")"
        else
            echo "`$tool: not found"
        fi
    done
} > "`$LOG_DIR/tools_available.txt" 2>&1

if command -v v4l2-ctl >/dev/null 2>&1; then
    v4l2-ctl --list-devices > "`$LOG_DIR/v4l2_list_devices.txt" 2>&1 || true
    v4l2-ctl -d /dev/video0 --all > "`$LOG_DIR/v4l2_video0_all.txt" 2>&1 || true
    v4l2-ctl -d /dev/video0 --list-formats-ext > "`$LOG_DIR/v4l2_video0_formats.txt" 2>&1 || true
fi

if command -v ffmpeg >/dev/null 2>&1; then
    timeout 8 ffmpeg -hide_banner -f v4l2 -list_formats all -i /dev/video0 -frames:v 1 -f null - > "`$LOG_DIR/ffmpeg_video0_probe.txt" 2>&1 || true
fi

if command -v media-ctl >/dev/null 2>&1; then
    media-ctl -p > "`$LOG_DIR/media_ctl.txt" 2>&1 || true
fi

if command -v i2cdetect >/dev/null 2>&1; then
    for bus in /dev/i2c-*; do
        [ -e "`$bus" ] || continue
        bus_id="`${bus##*-}"
        i2cdetect -y "`$bus_id" > "`$LOG_DIR/i2cdetect_`$bus_id.txt" 2>&1 || true
    done
fi

{
    echo "[original configs]"
    find "`$ORIGINAL_DIR/configs" -maxdepth 4 -type f \( -name '*.yaml' -o -name '*.yml' -o -name '*.json' \) -print 2>/dev/null || true
    echo
    echo "[integrated configs]"
    find "`$REMOTE_DIR/configs" -maxdepth 4 -type f \( -name '*.yaml' -o -name '*.yml' -o -name '*.json' \) -print 2>/dev/null || true
    echo
    echo "[model files]"
    find "`$ORIGINAL_DIR/imodel" "`$REMOTE_DIR/imodel" -maxdepth 4 -type f 2>/dev/null | sort || true
} > "`$LOG_DIR/config_inventory.txt" 2>&1

copy_if_exists() {
    src="`$1"
    dst="`$2"
    if [ -f "`$src" ]; then
        cp "`$src" "`$LOG_DIR/`$dst"
    fi
}

copy_if_exists "`$ORIGINAL_DIR/configs/ZG/sdicamera+yolov5+hdmi.yaml" "original_sdicamera_yolov5_hdmi.yaml"
copy_if_exists "`$REMOTE_DIR/configs/ZG/sdicamera+yolov5+hdmi.yaml" "integrated_sdicamera_yolov5_hdmi.yaml"
copy_if_exists "`$REMOTE_DIR/configs/ZG/sdicamera+yolov5+hdmi_board_model_direct.yaml" "integrated_board_model_direct.yaml"

{
    echo "[camera/input related config lines]"
    for f in "`$LOG_DIR"/*.yaml "`$LOG_DIR"/*.yml "`$LOG_DIR"/*.json; do
        [ -f "`$f" ] || continue
        echo
        echo "## `$(basename "`$f")"
        grep -nEi 'camera|input|sdi|hdmi|vtc|width|height|fps|model|detpost|ocm|imodel|video|image' "`$f" || true
    done
} > "`$LOG_DIR/config_camera_lines.txt" 2>&1

python3 - "`$LOG_DIR" <<'PY' > "`$LOG_DIR/summary.md"
import re
import sys
from pathlib import Path

log_dir = Path(sys.argv[1])

def text(name):
    path = log_dir / name
    return path.read_text(errors="ignore") if path.exists() else ""

devices = text("devices.txt")
tools = text("tools_available.txt")
dmesg = text("dmesg_camera.txt")
v4l2_all = text("v4l2_video0_all.txt")
ffmpeg_probe = text("ffmpeg_video0_probe.txt")
config_lines = text("config_camera_lines.txt")

video_nodes = sorted(set(re.findall(r"(/dev/video\d+)", devices)))
media_nodes = sorted(set(re.findall(r"(/dev/media\d+)", devices)))
i2c_nodes = sorted(set(re.findall(r"(/dev/i2c-\d+)", devices)))
has_v4l2_ctl = "v4l2-ctl: not found" not in tools and "v4l2-ctl:" in tools
video0_inappropriate = (
    "Inappropriate ioctl" in v4l2_all
    or "Inappropriate ioctl" in text("v4l2_video0_formats.txt")
    or "Inappropriate ioctl" in ffmpeg_probe
)
video0_is_mvx = "Driver name      : mvx" in v4l2_all or "Linlon Video device" in v4l2_all
video0_tiny_format = "Width/Height      : 2/2" in v4l2_all
mentions_timeout = bool(re.search(r"ImageMake|imk|accept 0 data", dmesg, re.I))
camera_type = sorted(set(re.findall(r"type:\s*([A-Za-z0-9_+-]+)", config_lines)))
camera_size = sorted(set(re.findall(r"width:\s*(\d+).*?\n.*?height:\s*(\d+)", config_lines, flags=re.S)))

print("# 30TAI Camera/PL Input Path Diagnostic Summary")
print()
print(f"- Video nodes: `{', '.join(video_nodes) if video_nodes else 'none'}`")
print(f"- Media nodes: `{', '.join(media_nodes) if media_nodes else 'none'}`")
print(f"- I2C nodes: `{', '.join(i2c_nodes) if i2c_nodes else 'none'}`")
print(f"- v4l2-ctl available: `{'YES' if has_v4l2_ctl else 'NO'}`")
print(f"- /dev/video0 standard V4L2 ioctl failure: `{'YES' if video0_inappropriate else 'NO/UNKNOWN'}`")
print(f"- /dev/video0 reports Linlon/mvx device: `{'YES' if video0_is_mvx else 'NO/UNKNOWN'}`")
print(f"- /dev/video0 reports 2x2 default format: `{'YES' if video0_tiny_format else 'NO/UNKNOWN'}`")
print(f"- Config camera type values: `{', '.join(camera_type) if camera_type else 'unknown'}`")
print(f"- dmesg mentions ImageMake/imk/input errors: `{'YES' if mentions_timeout else 'NO'}`")
print()
print("## Interpretation")
print()
if video0_is_mvx:
    print("- `/dev/video0` is reported as `mvx / Linlon Video device`; this looks like a codec or memory-to-memory video device, not the physical camera input used by the PLin pipeline.")
if video0_inappropriate:
    print("- `/dev/video0` does not behave like a normal V4L2 capture source, so PC-style camera capture tests are not sufficient for this board.")
else:
    print("- No clear standard-V4L2 failure was parsed from `/dev/video0`; inspect the raw V4L2 logs if the PLin path still receives zero data.")
if "hdmi" in [c.lower() for c in camera_type]:
    print("- The active PLin configs declare `camera.type: hdmi`; make sure the physical camera source is connected to the HDMI/SDI path expected by this bitstream and not only to a PC-side USB/V4L2 path.")
print("- The PLin application should be judged through its board-specific PL camera path and ImageMake logs.")
if config_lines.strip():
    print("- Camera/model related config excerpts were collected in `config_camera_lines.txt` for comparison.")
print()
print("## Next Checks")
print()
print("- Verify camera power, physical input connector, cable direction, and whether the camera output mode matches the PLin SDI input expected by the bitstream.")
print("- Compare `original_sdicamera_yolov5_hdmi.yaml` and `integrated_board_model_direct.yaml` in this log directory.")
print("- If the original demo also reports 'accept 0 data', fix the camera/PL input path before tuning aim/follow parameters.")
PY

echo "`$LOG_DIR"
"@

Write-Host ""
Write-Host "[Upload and run remote camera path diagnostic]" -ForegroundColor Cyan
& ssh @SshOptionArgs $SshTarget "mkdir -p '$RemoteLogDir'"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to create remote log dir: $RemoteLogDir"
}

$localScript = Join-Path $env:TEMP "diagnose_30tai_camera_path.sh"
Set-Content -LiteralPath $localScript -Value $remoteScript -Encoding ASCII
& scp @SshOptionArgs $localScript "${SshTarget}:$RemoteLogDir/diagnose_camera_path.sh"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to upload camera path diagnostic script."
}

& ssh @SshOptionArgs $SshTarget "bash '$RemoteLogDir/diagnose_camera_path.sh'"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Remote camera path diagnostic returned nonzero; fetching logs anyway." -ForegroundColor Yellow
}

New-Item -ItemType Directory -Force -Path $LocalLogDir | Out-Null
$localRunDir = Join-Path $LocalLogDir (Split-Path $RemoteLogDir -Leaf)

Write-Host ""
Write-Host "[Fetch camera path diagnostic logs]" -ForegroundColor Cyan
& scp -r @SshOptionArgs "${SshTarget}:$RemoteLogDir" "$localRunDir"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to fetch camera path diagnostic logs."
}

Write-Host ""
Write-Host "Fetched camera path diagnostic logs to: $localRunDir" -ForegroundColor Green
$summaryPath = Join-Path $localRunDir "summary.md"
if (Test-Path $summaryPath) {
    Write-Host ""
    Write-Host "[Camera path diagnostic summary]" -ForegroundColor Cyan
    Get-Content -Raw $summaryPath
}
