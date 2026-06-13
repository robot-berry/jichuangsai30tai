param(
    [string]$BoardIp = "192.168.125.171",
    [string]$User = "root",
    [string]$RemoteDir = "/home/work/fpai_demo_app/examples/codex/fpai_demo_src",
    [string]$RemoteSmokeLogDir = "/tmp/aim_follow_smoke",
    [string]$LocalLogDir = "",
    [string]$ProjectDir = "",
    [string]$SshKey = "",
    [switch]$Build,
    [switch]$SmokeTest,
    [switch]$FetchLogs,
    [switch]$LowMemoryBuild,
    [switch]$UseBoardReferenceModel,
    [string]$SmokeConfigPath = "configs/ZG/sdicamera+yolov5+hdmi.yaml",
    [string]$BoardModelJson = "/home/work/fpai_demo_app/examples/1_single_input+ai/PLin+SingleNet+HDMI/imodel/ZG/yolov5s_352x640_ZG.json",
    [string]$BoardModelRaw = "/home/work/fpai_demo_app/examples/1_single_input+ai/PLin+SingleNet+HDMI/imodel/ZG/yolov5s_352x640_ZG.raw",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
    throw "ProjectDir is required. Pass the integrated PLin application root, for example: -ProjectDir G:\UESTC\uav\01\PLin+SingleNet+HDMI\PLin+SingleNet+HDMI"
} else {
    $ProjectDir = (Resolve-Path $ProjectDir).Path
}

$BuildScript = Join-Path $ProjectDir "build_30tai.sh"
if (-not (Test-Path $BuildScript)) {
    throw "build_30tai.sh was not found under ProjectDir: $ProjectDir. This deploy script must run against the integrated PLin application project, not this module-only repository."
}

$ArchiveName = "plin_singlenet_hdmi_30tai.tar.gz"
$ArchivePath = Join-Path $env:TEMP $ArchiveName
$RemoteArchive = "/tmp/$ArchiveName"
$SshTarget = "$User@$BoardIp"
$SshOptionArgs = @("-o", "ConnectTimeout=10")
if (-not [string]::IsNullOrWhiteSpace($SshKey)) {
    $SshKey = (Resolve-Path $SshKey).Path
    $SshOptionArgs += @("-i", $SshKey)
}
$SshOptions = ($SshOptionArgs | ForEach-Object { "'$_'" }) -join " "
if ([string]::IsNullOrWhiteSpace($LocalLogDir)) {
    $LocalLogDir = Join-Path $ProjectDir "board_smoke_logs"
}

function Run-Step {
    param(
        [string]$Title,
        [string]$Command
    )
    Write-Host ""
    Write-Host "[$Title]" -ForegroundColor Cyan
    Write-Host $Command
    if (-not $DryRun) {
        Invoke-Expression $Command
    }
}

function Test-TcpPortFast {
    param(
        [string]$HostName,
        [int]$Port,
        [int]$TimeoutMs
    )

    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $task = $client.ConnectAsync($HostName, $Port)
        if (-not $task.Wait($TimeoutMs)) {
            return $false
        }
        return $client.Connected
    } catch {
        return $false
    } finally {
        $client.Close()
    }
}

Write-Host "ProjectDir: $ProjectDir"
Write-Host "Board:      $SshTarget"
Write-Host "RemoteDir:  $RemoteDir"
Write-Host "RemoteLog:  $RemoteSmokeLogDir"
Write-Host "LocalLog:   $LocalLogDir"
if (-not [string]::IsNullOrWhiteSpace($SshKey)) {
    Write-Host "SSH key:    $SshKey"
}

Run-Step "Check SSH port" "powershell -NoProfile -Command `"`$client = New-Object System.Net.Sockets.TcpClient; `$task = `$client.ConnectAsync('$BoardIp',22); if (`$task.Wait(3000)) { `$client.Connected } else { `$false }; `$client.Close()`""

if (-not $DryRun) {
    $canConnect = Test-TcpPortFast -HostName $BoardIp -Port 22 -TimeoutMs 3000
    if (-not $canConnect) {
        throw "Cannot connect to $BoardIp:22. Check board power, network cable, IP address, and PC network segment."
    }
}

if (Test-Path $ArchivePath) {
    Remove-Item -LiteralPath $ArchivePath -Force
}

$tarArgs = @(
    "-czf", $ArchivePath,
    "--exclude=build",
    "--exclude=.git",
    "--exclude=board_smoke_logs",
    "--exclude=aim_follow_control/build",
    "--exclude=aim_follow_control/build*",
    "--exclude=aim_follow_control/build_local_check_*",
    "--exclude=docs/*_render",
    "-C", (Split-Path $ProjectDir -Parent),
    (Split-Path $ProjectDir -Leaf)
)

Write-Host ""
Write-Host "[Create archive]" -ForegroundColor Cyan
Write-Host ("tar " + ($tarArgs -join " "))
if (-not $DryRun) {
    & tar @tarArgs
}

Run-Step "Upload archive" "scp $SshOptions '$ArchivePath' '${SshTarget}:$RemoteArchive'"

$remoteSetup = "rm -rf '$RemoteDir' && mkdir -p '$RemoteDir' && tar -xzf '$RemoteArchive' -C '$RemoteDir' --strip-components=1 && chmod +x '$RemoteDir/build_30tai.sh' '$RemoteDir/aim_follow_control/test/run_30tai_smoke_test.sh'"
Run-Step "Unpack on board" "ssh $SshOptions '$SshTarget' `"$remoteSetup`""

if ($Build) {
    if ($LowMemoryBuild) {
        $remoteBuild = "chmod 600 /swapfile 2>/dev/null || true; swapon /swapfile 2>/dev/null || true; cd '$RemoteDir' && cmake -S . -B build/ZG -DTARGET_CHIP=ZG -DCMAKE_PREFIX_PATH=/usr/cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_CXX_FLAGS='-O0 -g0' -DCMAKE_C_FLAGS='-O0 -g0' && cmake --build build/ZG -j1 && cmake --build build/ZG --target stage_bundle"
    } else {
        $remoteBuild = "cd '$RemoteDir' && ./build_30tai.sh"
    }
    Run-Step "Build on board" "ssh $SshOptions '$SshTarget' `"$remoteBuild`""
}

if ($UseBoardReferenceModel) {
    $boardSmokeConfigPath = "configs/ZG/sdicamera+yolov5+hdmi_board_model.yaml"
    $remoteConfig = "cd '$RemoteDir/build/ZG/deploy/ZG' && cp 'configs/ZG/sdicamera+yolov5+hdmi.yaml' '$boardSmokeConfigPath' && sed -i 's#./imodel/ZG/yolov5s_plin_352x640_ZG.json#$BoardModelJson#' '$boardSmokeConfigPath' && sed -i 's#./imodel/ZG/yolov5s_plin_352x640_ZG.raw#$BoardModelRaw#' '$boardSmokeConfigPath' && sed -i 's/ocm_option: 1/ocm_option: 0/' '$boardSmokeConfigPath' && sed -i 's/detpost: true/detpost: false/' '$boardSmokeConfigPath'"
    Run-Step "Create board reference model config" "ssh $SshOptions '$SshTarget' `"$remoteConfig`""
    $SmokeConfigPath = $boardSmokeConfigPath
}

if ($SmokeTest) {
    $remoteSmoke = "cd '$RemoteDir' && RUN_SECONDS=20 ./aim_follow_control/test/run_30tai_smoke_test.sh '$RemoteDir/build/ZG/deploy/ZG' sdicamera+yolov5+hdmi '$SmokeConfigPath' '$RemoteSmokeLogDir'"
    Run-Step "Run smoke test" "ssh $SshOptions '$SshTarget' `"$remoteSmoke`""
}

if ($FetchLogs) {
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $localRunLogDir = Join-Path $LocalLogDir "smoke_$timestamp"
    if (-not $DryRun) {
        New-Item -ItemType Directory -Force -Path $localRunLogDir | Out-Null
    }
    Run-Step "Fetch smoke logs" "scp -r $SshOptions '${SshTarget}:$RemoteSmokeLogDir/*' '$localRunLogDir/'"
    Write-Host "Fetched smoke logs to: $localRunLogDir"
}

Write-Host ""
Write-Host "Deploy script finished." -ForegroundColor Green
Write-Host "Manual board build:"
Write-Host "  ssh $SshTarget `"cd '$RemoteDir' && ./build_30tai.sh`""
Write-Host "Manual smoke test after build:"
Write-Host "  ssh $SshTarget `"cd '$RemoteDir' && RUN_SECONDS=20 ./aim_follow_control/test/run_30tai_smoke_test.sh '$RemoteDir/build/ZG/deploy/ZG' sdicamera+yolov5+hdmi configs/ZG/sdicamera+yolov5+hdmi.yaml '$RemoteSmokeLogDir'`""
Write-Host "Manual fetch smoke logs:"
Write-Host "  scp -r ${SshTarget}:$RemoteSmokeLogDir/* `"$LocalLogDir`""
