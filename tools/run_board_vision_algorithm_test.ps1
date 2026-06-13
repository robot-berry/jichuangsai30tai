param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,
    [string]$BoardIp = "192.168.125.171",
    [string]$User = "root",
    [string]$RemoteDir = "/home/work/fpai_demo_app/examples/codex/fpai_demo_src",
    [string]$RemoteLogDir = "/tmp/aim_follow_vision_algorithm",
    [string]$LocalLogDir = "",
    [string]$SshKey = "",
    [int]$RunSeconds = 20,
    [switch]$UseVtc,
    [switch]$SkipUpload,
    [switch]$SkipBuild,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ProjectDir = (Resolve-Path $ProjectDir).Path
if ([string]::IsNullOrWhiteSpace($LocalLogDir)) {
    $LocalLogDir = Join-Path $ProjectDir "board_vision_algorithm_logs"
}

$SshTarget = "$User@$BoardIp"
$SshOptionArgs = @("-o", "ConnectTimeout=10", "-o", "StrictHostKeyChecking=no")
if ($SshKey) {
    $SshKey = (Resolve-Path $SshKey).Path
    $SshOptionArgs += @("-i", $SshKey)
}

$ArchiveName = "plin_vision_algorithm_nocan.tar.gz"
$ArchivePath = Join-Path $env:TEMP $ArchiveName
$RemoteArchive = "/tmp/$ArchiveName"
$RunId = Get-Date -Format "yyyyMMdd_HHmmss"
$LocalRunLogDir = Join-Path $LocalLogDir "vision_$RunId"
$RemoteConfigPath = if ($UseVtc) {
    "configs/ZG/sdicamera+yolov5+hdmi_vision_vtc.yaml"
} else {
    "configs/ZG/sdicamera+yolov5+hdmi_vision_sdi.yaml"
}

function Run-Step {
    param(
        [string]$Title,
        [scriptblock]$Body,
        [string]$Preview
    )
    Write-Host ""
    Write-Host "[$Title]" -ForegroundColor Cyan
    if ($Preview) {
        Write-Host $Preview
    }
    if (-not $DryRun) {
        & $Body
    }
}

Write-Host "==== 30TAI vision/algorithm test without CAN control ====" -ForegroundColor Cyan
Write-Host "ProjectDir: $ProjectDir"
Write-Host "Board:      $SshTarget"
Write-Host "RemoteDir:  $RemoteDir"
Write-Host "RemoteLog:  $RemoteLogDir"
Write-Host "LocalLog:   $LocalRunLogDir"
Write-Host "RunSeconds: $RunSeconds"
Write-Host "InputMode:  $(if ($UseVtc) { 'VTC internal source' } else { 'Real SDI camera' })"
Write-Host "CAN:        DRYRUN, no can0 write"
Write-Host "SkipUpload: $SkipUpload"
Write-Host "SkipBuild:  $SkipBuild"
if ($SshKey) {
    Write-Host "SSH key:    $SshKey"
}

if (-not $SkipUpload) {
    if (Test-Path $ArchivePath) {
        Remove-Item -LiteralPath $ArchivePath -Force
    }

    $tarArgs = @(
        "-czf", $ArchivePath,
        "--exclude=build",
        "--exclude=.git",
        "--exclude=board_*",
        "--exclude=docs/*_render",
        "-C", (Split-Path $ProjectDir -Parent),
        (Split-Path $ProjectDir -Leaf)
    )
    Run-Step "Create archive" {
        & tar @tarArgs
    } ("tar " + ($tarArgs -join " "))

    Run-Step "Upload archive" {
        & scp @SshOptionArgs $ArchivePath "${SshTarget}:$RemoteArchive"
    } "scp <archive> ${SshTarget}:$RemoteArchive"

    $remoteSetup = "rm -rf '$RemoteDir' && mkdir -p '$RemoteDir' && tar --warning=no-timestamp -xzf '$RemoteArchive' -C '$RemoteDir' --strip-components=1 && find '$RemoteDir' -exec touch -h {} + && chmod +x '$RemoteDir/build_30tai.sh' '$RemoteDir/aim_follow_control/test/run_30tai_smoke_test.sh'"
    Run-Step "Unpack on board" {
        & ssh @SshOptionArgs $SshTarget $remoteSetup
    } "ssh $SshTarget `"$remoteSetup`""
}

if (-not $SkipBuild) {
    $remoteBuild = "cd '$RemoteDir' && chmod 600 /swapfile 2>/dev/null || true; swapon /swapfile 2>/dev/null || true; cmake -S . -B build/ZG -DTARGET_CHIP=ZG -DCMAKE_PREFIX_PATH=/usr/cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_CXX_FLAGS='-O0 -g0' -DCMAKE_C_FLAGS='-O0 -g0' && cmake --build build/ZG -j1"
    Run-Step "Build executable on board" {
        & ssh @SshOptionArgs $SshTarget $remoteBuild
    } "ssh $SshTarget `"$remoteBuild`""
}

$remoteConfig = @"
cd '$RemoteDir' &&
cp configs/ZG/sdicamera+yolov5+hdmi.yaml configs/ZG/sdicamera+yolov5+hdmi_vision_sdi.yaml &&
cp configs/ZG/sdicamera+yolov5+hdmi.yaml configs/ZG/sdicamera+yolov5+hdmi_vision_vtc.yaml &&
sed -i 's/ocm_option: 1/ocm_option: 0/' configs/ZG/sdicamera+yolov5+hdmi_vision_sdi.yaml configs/ZG/sdicamera+yolov5+hdmi_vision_vtc.yaml &&
sed -i 's/detpost: true/detpost: false/' configs/ZG/sdicamera+yolov5+hdmi_vision_sdi.yaml configs/ZG/sdicamera+yolov5+hdmi_vision_vtc.yaml &&
sed -i 's#./imodel/ZG/yolov5s_plin_352x640_ZG.json#/home/work/fpai_demo_app/examples/1_single_input+ai/PLin+SingleNet+HDMI/imodel/ZG/yolov5s_352x640_ZG.json#' configs/ZG/sdicamera+yolov5+hdmi_vision_sdi.yaml configs/ZG/sdicamera+yolov5+hdmi_vision_vtc.yaml &&
sed -i 's#./imodel/ZG/yolov5s_plin_352x640_ZG.raw#/home/work/fpai_demo_app/examples/1_single_input+ai/PLin+SingleNet+HDMI/imodel/ZG/yolov5s_352x640_ZG.raw#' configs/ZG/sdicamera+yolov5+hdmi_vision_sdi.yaml configs/ZG/sdicamera+yolov5+hdmi_vision_vtc.yaml &&
sed -i 's/vtc: false/vtc: true/' configs/ZG/sdicamera+yolov5+hdmi_vision_vtc.yaml
"@ -replace "`r?`n", " "

Run-Step "Create no-CAN vision configs" {
    & ssh @SshOptionArgs $SshTarget $remoteConfig
} "ssh $SshTarget `"<create vision configs>`""

$remoteRun = "rm -rf '$RemoteLogDir' && mkdir -p '$RemoteLogDir' && cd '$RemoteDir' && AIM_FOLLOW_CAN_DRYRUN=1 timeout '$RunSeconds' ./build/ZG/sdicamera+yolov5+hdmi '$RemoteConfigPath' > '$RemoteLogDir/app.log' 2>&1; ret=`$?; echo `$ret > '$RemoteLogDir/exit_code.txt'; grep -n 'ImageMake Timeout\|accept 0 data\|\[CAN DRYRUN\]\|\[AIM FOLLOW\]\|\[DISTANCE DEBUG\]\|\[YOLO DEBUG\]\|All actors started\|All actors stopped' '$RemoteLogDir/app.log' | tail -120 || true; exit 0"
Run-Step "Run vision/algorithm test" {
    & ssh @SshOptionArgs $SshTarget $remoteRun
} "ssh $SshTarget `"$remoteRun`""

if ($DryRun) {
    Write-Host ""
    Write-Host "Dry run finished. No logs were fetched." -ForegroundColor Yellow
    exit 0
}

New-Item -ItemType Directory -Force -Path $LocalRunLogDir | Out-Null
Run-Step "Fetch logs" {
    & scp -r @SshOptionArgs "${SshTarget}:$RemoteLogDir/*" "$LocalRunLogDir/"
} "scp -r ${SshTarget}:$RemoteLogDir/* $LocalRunLogDir/"

$analyzeArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $RepoRoot "tools\analyze_vision_algorithm_logs.ps1"),
    "-LogDir", $LocalRunLogDir
)
if ($UseVtc) {
    $analyzeArgs += "-AllowNoTarget"
}
& powershell @analyzeArgs
exit $LASTEXITCODE
