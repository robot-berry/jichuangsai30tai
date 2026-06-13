param(
    [string]$BoardIp = "192.168.125.171",
    [string]$User = "root",
    [string]$SshKey = "",
    [string]$RemoteDir = "/home/work/fpai_demo_app/examples/codex/fpai_demo_src",
    [string]$OriginalDir = "/home/work/fpai_demo_app/examples/1_single_input+ai/PLin+SingleNet+HDMI",
    [int]$RunSeconds = 12,
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
    $LocalLogDir = Join-Path $RepoRoot "board_video_diagnostics"
}

$RemoteLogDir = "/tmp/aim_follow_video_diag_$(Get-Date -Format 'yyyyMMdd_HHmmss')"

Write-Host "==== 30TAI video input diagnostic ====" -ForegroundColor Cyan
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

REMOTE_LOG_DIR='$RemoteLogDir'
REMOTE_DIR='$RemoteDir'
ORIGINAL_DIR='$OriginalDir'
RUN_SECONDS='$RunSeconds'

mkdir -p "`$REMOTE_LOG_DIR"
PROGRESS="`$REMOTE_LOG_DIR/progress.txt"
echo "start" > "`$PROGRESS"

run_case() {
    name="`$1"
    workdir="`$2"
    app="`$3"
    config="`$4"
    log="`$REMOTE_LOG_DIR/`$name.log"

    echo "before `$name" >> "`$PROGRESS"
    (
        echo "[case] `$name"
        echo "[workdir] `$workdir"
        echo "[app] `$app"
        echo "[config] `$config"
        if [ ! -x "`$workdir/`$app" ]; then
            echo "[skip] executable not found: `$workdir/`$app"
            exit 0
        fi
        if [ ! -f "`$workdir/`$config" ]; then
            echo "[skip] config not found: `$workdir/`$config"
            exit 0
        fi
        cd "`$workdir"
        timeout "`$RUN_SECONDS" "./`$app" "`$config"
        ret=`$?
        echo "[ret] `$ret"
        exit 0
    ) > "`$log" 2>&1
    echo "after `$name" >> "`$PROGRESS"
}

{
    echo "[date]"
    date
    echo
    echo "[process]"
    ps -ef | grep -E 'sdicamera|yolov5|fpai' | grep -v grep || true
    echo
    echo "[devices]"
    ls -l /dev/video* /dev/media* /dev/fb* /dev/dri/* 2>/dev/null || true
    echo
    echo "[video0 ffmpeg probe]"
    timeout 8 ffmpeg -hide_banner -f v4l2 -list_formats all -i /dev/video0 -frames:v 1 -f null - 2>&1 || true
    echo
    echo "[dmesg tail]"
    dmesg | tail -n 120 || true
} > "`$REMOTE_LOG_DIR/system.txt" 2>&1

run_case original_plin "`$ORIGINAL_DIR" "sdicamera+yolov5+hdmi" "configs/ZG/sdicamera+yolov5+hdmi.yaml"
echo "after original call" >> "`$PROGRESS"

DEPLOY="`$REMOTE_DIR/build/ZG/deploy/ZG"
DIRECT="`$REMOTE_DIR"
echo "deploy=`$DEPLOY" >> "`$PROGRESS"
echo "direct=`$DIRECT" >> "`$PROGRESS"
run_case integrated_board_model "`$DEPLOY" "sdicamera+yolov5+hdmi" "configs/ZG/sdicamera+yolov5+hdmi_board_model.yaml"
echo "after integrated call" >> "`$PROGRESS"
run_case integrated_direct "`$DIRECT" "build/ZG/sdicamera+yolov5+hdmi" "configs/ZG/sdicamera+yolov5+hdmi_board_model_direct.yaml"
echo "after integrated direct call" >> "`$PROGRESS"

if [ -f "`$DIRECT/configs/ZG/sdicamera+yolov5+hdmi_board_model_direct.yaml" ]; then
    cp "`$DIRECT/configs/ZG/sdicamera+yolov5+hdmi_board_model_direct.yaml" "`$DIRECT/configs/ZG/sdicamera+yolov5+hdmi_board_model_direct_vtc.yaml"
    sed -i 's/vtc: false/vtc: true/' "`$DIRECT/configs/ZG/sdicamera+yolov5+hdmi_board_model_direct_vtc.yaml"
fi
run_case integrated_direct_vtc "`$DIRECT" "build/ZG/sdicamera+yolov5+hdmi" "configs/ZG/sdicamera+yolov5+hdmi_board_model_direct_vtc.yaml"
echo "after integrated direct vtc call" >> "`$PROGRESS"

if [ -f "`$DEPLOY/configs/ZG/sdicamera+yolov5+hdmi_board_model.yaml" ]; then
    cp "`$DEPLOY/configs/ZG/sdicamera+yolov5+hdmi_board_model.yaml" "`$DEPLOY/configs/ZG/sdicamera+yolov5+hdmi_board_model_vtc.yaml"
    sed -i 's/vtc: false/vtc: true/' "`$DEPLOY/configs/ZG/sdicamera+yolov5+hdmi_board_model_vtc.yaml"
fi
run_case integrated_vtc "`$DEPLOY" "sdicamera+yolov5+hdmi" "configs/ZG/sdicamera+yolov5+hdmi_board_model_vtc.yaml"
echo "after vtc call" >> "`$PROGRESS"

{
    echo "# 30TAI Video Input Diagnostic Summary"
    echo
    echo "| Case | All actors | ImageMake timeout | accept 0 data | AIM config | AIM follow | Distance debug | Abort/runtime error |"
    echo "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
    for case_name in original_plin integrated_board_model integrated_direct integrated_direct_vtc integrated_vtc; do
        log="`$REMOTE_LOG_DIR/`$case_name.log"
        if [ ! -f "`$log" ]; then
            echo "| `$case_name | 0 | 0 | 0 | 0 | 0 | 0 | 0 |"
            continue
        fi
        all_actors=`$(grep -c 'All actors started' "`$log" || true)
        imk_timeout=`$(grep -c 'ImageMake Timeout' "`$log" || true)
        accept_zero=`$(grep -c 'accept 0 data' "`$log" || true)
        aim_config=`$(grep -c '\[AIM FOLLOW CONFIG\]' "`$log" || true)
        aim_follow=`$(grep -c '\[AIM FOLLOW\]' "`$log" || true)
        dist_debug=`$(grep -c '\[DISTANCE DEBUG\]' "`$log" || true)
        aborts=`$(grep -c -E 'terminate called|runtime_error|Aborted|No DetPost HardWare' "`$log" || true)
        echo "| `$case_name | `$all_actors | `$imk_timeout | `$accept_zero | `$aim_config | `$aim_follow | `$dist_debug | `$aborts |"
    done
    echo
    echo "Interpretation:"
    echo
    if grep -q 'accept 0 data' "`$REMOTE_LOG_DIR/original_plin.log" 2>/dev/null; then
        echo "- The unmodified board PLin demo also receives zero ImageMake input data. This points to SDI/camera/bitstream input state, not the aim/follow module."
    fi
    if grep -q 'ioctl(VIDIOC_G_INPUT).*Inappropriate ioctl' "`$REMOTE_LOG_DIR/system.txt" 2>/dev/null; then
        echo "- /dev/video0 does not behave as a normal V4L2 capture device on this image; the PLin demo depends on the board-specific PL camera path."
    fi
    if grep -q 'IcoreActor: Failed to pop from input queue' "`$REMOTE_LOG_DIR/integrated_direct_vtc.log" "`$REMOTE_LOG_DIR/integrated_vtc.log" 2>/dev/null; then
        echo "- The VTC/test-pattern path avoids the zero-data ImageMake symptom but currently stops the input queue, so it is useful only as a diagnostic isolation check."
    fi
} > "`$REMOTE_LOG_DIR/summary.md"

echo "`$REMOTE_LOG_DIR"
"@

Write-Host ""
Write-Host "[Upload and run remote diagnostic]" -ForegroundColor Cyan
& ssh @SshOptionArgs $SshTarget "mkdir -p '$RemoteLogDir'"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to create remote diagnostic directory: $RemoteLogDir"
}

$localScript = Join-Path $env:TEMP "diagnose_30tai_video_input.sh"
Set-Content -LiteralPath $localScript -Value $remoteScript -Encoding ASCII
& scp @SshOptionArgs $localScript "${SshTarget}:$RemoteLogDir/diagnose.sh"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to upload remote diagnostic script."
}

& ssh @SshOptionArgs $SshTarget "bash '$RemoteLogDir/diagnose.sh'"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Remote diagnostic returned a nonzero status; logs will still be fetched." -ForegroundColor Yellow
}

New-Item -ItemType Directory -Force -Path $LocalLogDir | Out-Null
$localRunDir = Join-Path $LocalLogDir (Split-Path $RemoteLogDir -Leaf)

Write-Host ""
Write-Host "[Fetch diagnostic logs]" -ForegroundColor Cyan
& scp -r @SshOptionArgs "${SshTarget}:$RemoteLogDir" "$localRunDir"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to fetch diagnostic logs from $RemoteLogDir"
}

Write-Host ""
Write-Host "Fetched diagnostic logs to: $localRunDir" -ForegroundColor Green
Write-Host "Summary: $(Join-Path $localRunDir 'summary.md')"
