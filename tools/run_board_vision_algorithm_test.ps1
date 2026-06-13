param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,
    [string]$BoardIp = "192.168.125.171",
    [string]$User = "root",
    [string]$RemoteDir = "/home/work/fpai_demo_app/examples/codex/fpai_demo_src",
    [string]$RemoteSmokeLogDir = "/tmp/aim_follow_vision_algorithm",
    [string]$LocalLogDir = "",
    [string]$SshKey = "",
    [int]$RunSeconds = 20,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ProjectDir = (Resolve-Path $ProjectDir).Path
if ([string]::IsNullOrWhiteSpace($LocalLogDir)) {
    $LocalLogDir = Join-Path $ProjectDir "board_vision_algorithm_logs"
}

function Get-LatestLogDir {
    param([string]$Root)
    if (-not (Test-Path $Root)) {
        return $null
    }
    return Get-ChildItem -LiteralPath $Root -Directory |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}

Write-Host "==== 30TAI vision/algorithm test without CAN control ====" -ForegroundColor Cyan
Write-Host "ProjectDir: $ProjectDir"
Write-Host "Board:      $User@$BoardIp"
Write-Host "RemoteDir:  $RemoteDir"
Write-Host "RunSeconds: $RunSeconds"
Write-Host "LocalLog:   $LocalLogDir"
if ($SshKey) {
    $SshKey = (Resolve-Path $SshKey).Path
    Write-Host "SSH key:    $SshKey"
}
Write-Host "CAN:        DRYRUN, no can0 write"

$beforeLatest = Get-LatestLogDir -Root $LocalLogDir

$deployArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $RepoRoot "tools\deploy_30tai.ps1"),
    "-ProjectDir", $ProjectDir,
    "-BoardIp", $BoardIp,
    "-User", $User,
    "-RemoteDir", $RemoteDir,
    "-RemoteSmokeLogDir", $RemoteSmokeLogDir,
    "-LocalLogDir", $LocalLogDir,
    "-Build",
    "-LowMemoryBuild",
    "-UseBoardReferenceModel",
    "-SmokeTest",
    "-FetchLogs"
)
if ($SshKey) {
    $deployArgs += @("-SshKey", $SshKey)
}
if ($DryRun) {
    $deployArgs += "-DryRun"
}

$oldRunSeconds = $env:RUN_SECONDS
$oldCanDryRun = $env:AIM_FOLLOW_CAN_DRYRUN
try {
    $env:RUN_SECONDS = "$RunSeconds"
    $env:AIM_FOLLOW_CAN_DRYRUN = "1"
    & powershell @deployArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Deploy/build/vision smoke test failed."
    }
} finally {
    $env:RUN_SECONDS = $oldRunSeconds
    $env:AIM_FOLLOW_CAN_DRYRUN = $oldCanDryRun
}

if ($DryRun) {
    Write-Host ""
    Write-Host "Dry run finished. No logs were analyzed." -ForegroundColor Yellow
    exit 0
}

$afterLatest = Get-LatestLogDir -Root $LocalLogDir
if (-not $afterLatest) {
    throw "No fetched vision/algorithm log directory found under: $LocalLogDir"
}
if ($beforeLatest -and ($afterLatest.FullName -eq $beforeLatest.FullName)) {
    Write-Host "Warning: latest log directory did not change; analyzing existing latest directory." -ForegroundColor Yellow
}

& powershell -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $RepoRoot "tools\analyze_vision_algorithm_logs.ps1") `
    -LogDir $afterLatest.FullName
exit $LASTEXITCODE
