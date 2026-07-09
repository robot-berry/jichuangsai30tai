param(
    [string]$BoardIp = "192.168.125.171",
    [string]$User = "root",
    [string]$SshKey = "",
    [string]$RemoteDir = "/home/work/fpai_demo_app/examples/codex/fpai_demo_src",
    [string]$BaseConfig = "configs/ZG/sdicamera+yolov5+hdmi_board_model_direct.yaml",
    [int]$RunSeconds = 6,
    [string]$Modes = "1920x1080@60,1920x1080@30,1280x720@60,1280x720@30,1920x1080@25,1280x720@50",
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
    $LocalLogDir = Join-Path $RepoRoot "board_sdi_mode_probes"
}

$RemoteLogDir = "/tmp/aim_follow_sdi_mode_probe_$(Get-Date -Format 'yyyyMMdd_HHmmss')"

Write-Host "==== 30TAI SDI mode probe ====" -ForegroundColor Cyan
Write-Host "Board:        $SshTarget"
Write-Host "RemoteDir:    $RemoteDir"
Write-Host "BaseConfig:   $BaseConfig"
Write-Host "RunSeconds:   $RunSeconds"
Write-Host "Modes:        $Modes"
Write-Host "RemoteLogDir: $RemoteLogDir"
Write-Host "LocalLogDir:  $LocalLogDir"
if (-not [string]::IsNullOrWhiteSpace($SshKey)) {
    Write-Host "SSH key:      $SshKey"
}

$remoteScript = @"
set +e

REMOTE_DIR='$RemoteDir'
BASE_CONFIG='$BaseConfig'
RUN_SECONDS='$RunSeconds'
MODES='$Modes'
LOG_DIR='$RemoteLogDir'

mkdir -p "`$LOG_DIR"
APP="build/ZG/sdicamera+yolov5+hdmi"

{
    echo "[date]"
    date
    echo
    echo "[remote_dir] `$REMOTE_DIR"
    echo "[base_config] `$BASE_CONFIG"
    echo "[app] `$APP"
    echo "[modes] `$MODES"
} > "`$LOG_DIR/probe_info.txt" 2>&1

if [ ! -x "`$REMOTE_DIR/`$APP" ]; then
    echo "Executable not found: `$REMOTE_DIR/`$APP" > "`$LOG_DIR/error.txt"
    echo "`$LOG_DIR"
    exit 0
fi

if [ ! -f "`$REMOTE_DIR/`$BASE_CONFIG" ]; then
    echo "Base config not found: `$REMOTE_DIR/`$BASE_CONFIG" > "`$LOG_DIR/error.txt"
    echo "`$LOG_DIR"
    exit 0
fi

mode_index=0
OLD_IFS="`$IFS"
IFS=','
for mode in `$MODES; do
    IFS="`$OLD_IFS"
    mode=`$(echo "`$mode" | tr -d ' ')
    width=`$(echo "`$mode" | sed -E 's/^([0-9]+)x([0-9]+)@([0-9]+)$/\1/')
    height=`$(echo "`$mode" | sed -E 's/^([0-9]+)x([0-9]+)@([0-9]+)$/\2/')
    fps=`$(echo "`$mode" | sed -E 's/^([0-9]+)x([0-9]+)@([0-9]+)$/\3/')
    if ! echo "`$width `$height `$fps" | grep -Eq '^[0-9]+ [0-9]+ [0-9]+$'; then
        echo "Skip invalid mode: `$mode" >> "`$LOG_DIR/probe_info.txt"
        IFS=','
        continue
    fi

    config="configs/ZG/sdicamera+yolov5+hdmi_probe_`$mode_index.yaml"
    log="`$LOG_DIR/probe_`$mode.log"
    cp "`$REMOTE_DIR/`$BASE_CONFIG" "`$REMOTE_DIR/`$config"

    python3 - "`$REMOTE_DIR/`$config" "`$width" "`$height" "`$fps" <<'PY'
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
width, height, fps = sys.argv[2:5]
lines = path.read_text().splitlines()
in_camera = False
camera_indent = None
for i, line in enumerate(lines):
    if re.match(r"^\s*camera:\s*$", line):
        in_camera = True
        camera_indent = len(line) - len(line.lstrip())
        continue
    if in_camera:
        indent = len(line) - len(line.lstrip())
        if line.strip() and indent <= camera_indent:
            in_camera = False
        elif re.match(r"^\s*width:\s*\d+", line):
            lines[i] = re.sub(r"\d+", width, line, count=1)
        elif re.match(r"^\s*height:\s*\d+", line):
            lines[i] = re.sub(r"\d+", height, line, count=1)
        elif re.match(r"^\s*fps:\s*\d+", line):
            lines[i] = re.sub(r"\d+", fps, line, count=1)
path.write_text("\n".join(lines) + "\n")
PY

    (
        echo "[mode] `$mode"
        echo "[config] `$config"
        echo "[camera lines]"
        grep -nA5 'camera:' "`$REMOTE_DIR/`$config" || true
        echo
        cd "`$REMOTE_DIR"
        timeout "`$RUN_SECONDS" "./`$APP" "`$config"
        ret=`$?
        echo "[ret] `$ret"
    ) > "`$log" 2>&1

    mode_index=`$((mode_index + 1))
    IFS=','
done
IFS="`$OLD_IFS"

python3 - "`$LOG_DIR" <<'PY' > "`$LOG_DIR/summary.md"
import re
import sys
from pathlib import Path

log_dir = Path(sys.argv[1])
rows = []
for path in sorted(log_dir.glob("probe_*.log")):
    text = path.read_text(errors="ignore")
    mode = re.search(r"\[mode\]\s*(\S+)", text)
    mode = mode.group(1) if mode else path.stem.replace("probe_", "")
    all_actors = len(re.findall(r"All actors started", text))
    imk_timeout = len(re.findall(r"ImageMake Timeout", text))
    accept_zero = len(re.findall(r"accept 0 data", text))
    aim_config = len(re.findall(r"\[AIM FOLLOW CONFIG\]", text))
    aborts = len(re.findall(r"terminate called|runtime_error|Aborted|No DetPost HardWare", text))
    ret = re.search(r"\[ret\]\s*(\d+)", text)
    ret = ret.group(1) if ret else "unknown"
    rows.append((mode, all_actors, imk_timeout, accept_zero, aim_config, aborts, ret))

print("# 30TAI SDI Mode Probe Summary")
print()
print("| Mode | All actors | ImageMake timeout | accept 0 data | AIM config | Abort/runtime error | ret |")
print("| --- | ---: | ---: | ---: | ---: | ---: | ---: |")
for row in rows:
    print(f"| `{row[0]}` | {row[1]} | {row[2]} | {row[3]} | {row[4]} | {row[5]} | `{row[6]}` |")
print()
if rows:
    candidates = [r[0] for r in rows if r[1] > 0 and r[2] == 0 and r[3] == 0 and r[5] == 0]
    if candidates:
        print("Candidate modes without ImageMake zero-data errors:")
        for mode in candidates:
            print(f"- `{mode}`")
    else:
        print("No tested real-SDI mode avoided ImageMake zero-data errors.")
print()
print("Interpretation: this probe only changes temporary YAML camera width/height/fps values. If every real-SDI mode still reports zero input data while VTC works, continue checking physical SDI signal, camera output mode, and bitstream support.")
PY

echo "`$LOG_DIR"
"@

Write-Host ""
Write-Host "[Upload and run SDI mode probe]" -ForegroundColor Cyan
& ssh @SshOptionArgs $SshTarget "mkdir -p '$RemoteLogDir'"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to create remote log dir: $RemoteLogDir"
}

$localScript = Join-Path $env:TEMP "probe_30tai_sdi_modes.sh"
Set-Content -LiteralPath $localScript -Value $remoteScript -Encoding ASCII
& scp @SshOptionArgs $localScript "${SshTarget}:$RemoteLogDir/probe_sdi_modes.sh"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to upload SDI mode probe script."
}

& ssh @SshOptionArgs $SshTarget "bash '$RemoteLogDir/probe_sdi_modes.sh'"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Remote SDI mode probe returned nonzero; fetching logs anyway." -ForegroundColor Yellow
}

New-Item -ItemType Directory -Force -Path $LocalLogDir | Out-Null
$localRunDir = Join-Path $LocalLogDir (Split-Path $RemoteLogDir -Leaf)

Write-Host ""
Write-Host "[Fetch SDI mode probe logs]" -ForegroundColor Cyan
& scp -r @SshOptionArgs "${SshTarget}:$RemoteLogDir" "$localRunDir"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to fetch SDI mode probe logs."
}

Write-Host ""
Write-Host "Fetched SDI mode probe logs to: $localRunDir" -ForegroundColor Green
$summaryPath = Join-Path $localRunDir "summary.md"
if (Test-Path $summaryPath) {
    Write-Host ""
    Write-Host "[SDI mode probe summary]" -ForegroundColor Cyan
    Get-Content -Raw $summaryPath
}
