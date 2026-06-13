param(
    [string]$BoardIp = "192.168.125.171",
    [string]$User = "root",
    [string]$SshKey = "",
    [string]$RemoteDir = "/home/work/fpai_demo_app/examples/codex/fpai_demo_src",
    [string]$RealConfig = "configs/ZG/sdicamera+yolov5+hdmi_board_model_direct.yaml",
    [int]$RunSeconds = 5,
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
    $LocalLogDir = Join-Path $RepoRoot "board_sdi_register_dumps"
}

$RemoteLogDir = "/tmp/aim_follow_sdi_regs_$(Get-Date -Format 'yyyyMMdd_HHmmss')"

Write-Host "==== 30TAI SDI register dump ====" -ForegroundColor Cyan
Write-Host "Board:        $SshTarget"
Write-Host "RemoteDir:    $RemoteDir"
Write-Host "RealConfig:   $RealConfig"
Write-Host "RunSeconds:   $RunSeconds"
Write-Host "RemoteLogDir: $RemoteLogDir"
Write-Host "LocalLogDir:  $LocalLogDir"
if (-not [string]::IsNullOrWhiteSpace($SshKey)) {
    Write-Host "SSH key:      $SshKey"
}

$remoteScript = @"
set +e

LOG_DIR='$RemoteLogDir'
REMOTE_DIR='$RemoteDir'
REAL_CONFIG='$RealConfig'
RUN_SECONDS='$RunSeconds'
APP='build/ZG/sdicamera+yolov5+hdmi'

mkdir -p "`$LOG_DIR"

dump_regs() {
    label="`$1"
    python3 - "`$label" "`$LOG_DIR" <<'PY'
import mmap
import os
import struct
import sys
from pathlib import Path

label = sys.argv[1]
log_dir = Path(sys.argv[2])

bases = {
    "sdi0": 0x40080000,
    "sdi1": 0x40080400,
    "sdi2": 0x40080800,
    "sdi3": 0x40080C00,
}

named_offsets = {
    0x04: "take",
    0x18: "ps_resize_enable",
    0x50: "write_addr",
    0x58: "done_status",
    0x5C: "ps_crop_x",
    0x60: "ps_crop_y",
    0x64: "camera_size",
    0x68: "ps_stride",
    0x6C: "ps_pixel_len",
    0x78: "ps_format",
    0x7C: "resize_pix_len",
    0x84: "wr_ps_done_to_next_take_cycles",
    0x90: "cycle_threshold_overrun_count",
    0xB4: "vtc_enable_or_done2fetch",
}

offsets = list(range(0x00, 0xC0, 4))
page_size = mmap.PAGESIZE
out = []
summary = []

def read32(fd, addr):
    page = addr & ~(page_size - 1)
    delta = addr - page
    mm = mmap.mmap(fd, page_size, flags=mmap.MAP_SHARED, prot=mmap.PROT_READ, offset=page)
    try:
        return struct.unpack_from("<I", mm, delta)[0]
    finally:
        mm.close()

fd = os.open("/dev/mem", os.O_RDONLY | getattr(os, "O_SYNC", 0))
try:
    for name, base in bases.items():
        out.append(f"# {label} {name} base=0x{base:08X}")
        out.append("| Offset | Name | Value | Bits |")
        out.append("| ---: | --- | ---: | --- |")
        values = {}
        for off in offsets:
            addr = base + off
            try:
                val = read32(fd, addr)
                values[off] = val
                nm = named_offsets.get(off, "")
                bits = f"{val:032b}" if off in (0x04, 0x18, 0x58, 0x78, 0xB4) else ""
                out.append(f"| 0x{off:03X} | {nm} | 0x{val:08X} | `{bits}` |")
            except Exception as exc:
                out.append(f"| 0x{off:03X} | {named_offsets.get(off, '')} | ERROR | `{exc}` |")
        done = values.get(0x58)
        vtc = values.get(0xB4)
        overrun = values.get(0x90)
        cycles = values.get(0x84)
        cam_size = values.get(0x64)
        if done is not None:
            summary.append(
                f"- {label} {name}: done_status=0x{done:08X} "
                f"(done_bit={done & 1}, error_bit={(done >> 2) & 1}), "
                f"vtc_or_done2fetch=0x{vtc:08X} "
                f"camera_size=0x{cam_size:08X} "
                f"cycles84=0x{cycles:08X} overrun90=0x{overrun:08X}"
            )
        out.append("")
finally:
    os.close(fd)

(log_dir / f"{label}_registers.md").write_text("\n".join(out) + "\n")
(log_dir / f"{label}_summary.txt").write_text("\n".join(summary) + "\n")
PY
}

run_case() {
    name="`$1"
    config="`$2"
    log="`$LOG_DIR/`$name.log"
    (
        echo "[case] `$name"
        echo "[config] `$config"
        if [ ! -x "`$REMOTE_DIR/`$APP" ]; then
            echo "[skip] executable not found: `$REMOTE_DIR/`$APP"
            exit 0
        fi
        if [ ! -f "`$REMOTE_DIR/`$config" ]; then
            echo "[skip] config not found: `$REMOTE_DIR/`$config"
            exit 0
        fi
        cd "`$REMOTE_DIR"
        timeout "`$RUN_SECONDS" "./`$APP" "`$config"
        ret=`$?
        echo "[ret] `$ret"
    ) > "`$log" 2>&1
}

{
    echo "[date]"
    date
    echo
    echo "[remote_dir] `$REMOTE_DIR"
    echo "[real_config] `$REAL_CONFIG"
    echo "[app] `$APP"
    echo "[note] Register snapshots are read-only via /dev/mem."
} > "`$LOG_DIR/info.txt"

dump_regs before

run_case real_sdi "`$REAL_CONFIG"
dump_regs after_real_sdi

VTC_CONFIG='configs/ZG/sdicamera+yolov5+hdmi_register_probe_vtc.yaml'
if [ -f "`$REMOTE_DIR/`$REAL_CONFIG" ]; then
    cp "`$REMOTE_DIR/`$REAL_CONFIG" "`$REMOTE_DIR/`$VTC_CONFIG"
    sed -i 's/vtc: false/vtc: true/' "`$REMOTE_DIR/`$VTC_CONFIG"
fi
run_case vtc "`$VTC_CONFIG"
dump_regs after_vtc

python3 - "`$LOG_DIR" <<'PY' > "`$LOG_DIR/summary.md"
import re
import sys
from pathlib import Path

log_dir = Path(sys.argv[1])

print("# 30TAI SDI Register Dump Summary")
print()
for name in ["before", "after_real_sdi", "after_vtc"]:
    path = log_dir / f"{name}_summary.txt"
    if path.exists():
        print(f"## {name}")
        print()
        print(path.read_text(errors="ignore").strip())
        print()

print("## Runtime Symptoms")
print()
print("| Case | All actors | ImageMake timeout | accept 0 data | AIM config | ret |")
print("| --- | ---: | ---: | ---: | ---: | ---: |")
for case in ["real_sdi", "vtc"]:
    path = log_dir / f"{case}.log"
    text = path.read_text(errors="ignore") if path.exists() else ""
    all_actors = len(re.findall(r"All actors started", text))
    imk_timeout = len(re.findall(r"ImageMake Timeout", text))
    accept_zero = len(re.findall(r"accept 0 data", text))
    aim_config = len(re.findall(r"\[AIM FOLLOW CONFIG\]", text))
    ret = re.search(r"\[ret\]\s*(\d+)", text)
    print(f"| `{case}` | {all_actors} | {imk_timeout} | {accept_zero} | {aim_config} | `{ret.group(1) if ret else 'unknown'}` |")

print()
print("Interpretation: compare 'after_real_sdi_registers.md' and 'after_vtc_registers.md'. If VTC runs while real SDI reports zero data, focus on external SDI signal lock and the SDI_IN_0 hardware path.")
PY

echo "`$LOG_DIR"
"@

Write-Host ""
Write-Host "[Upload and run SDI register dump]" -ForegroundColor Cyan
& ssh @SshOptionArgs $SshTarget "mkdir -p '$RemoteLogDir'"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to create remote log dir: $RemoteLogDir"
}

$localScript = Join-Path $env:TEMP "dump_30tai_sdi_registers.sh"
Set-Content -LiteralPath $localScript -Value $remoteScript -Encoding ASCII
& scp @SshOptionArgs $localScript "${SshTarget}:$RemoteLogDir/dump_sdi_registers.sh"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to upload SDI register dump script."
}

& ssh @SshOptionArgs $SshTarget "bash '$RemoteLogDir/dump_sdi_registers.sh'"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Remote SDI register dump returned nonzero; fetching logs anyway." -ForegroundColor Yellow
}

New-Item -ItemType Directory -Force -Path $LocalLogDir | Out-Null
$localRunDir = Join-Path $LocalLogDir (Split-Path $RemoteLogDir -Leaf)

Write-Host ""
Write-Host "[Fetch SDI register dump logs]" -ForegroundColor Cyan
& scp -r @SshOptionArgs "${SshTarget}:$RemoteLogDir" "$localRunDir"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to fetch SDI register dump logs."
}

Write-Host ""
Write-Host "Fetched SDI register dump logs to: $localRunDir" -ForegroundColor Green
$summaryPath = Join-Path $localRunDir "summary.md"
if (Test-Path $summaryPath) {
    Write-Host ""
    Write-Host "[SDI register dump summary]" -ForegroundColor Cyan
    Get-Content -Raw $summaryPath
}
