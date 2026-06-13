param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,
    [string]$BoardIp = "192.168.125.171",
    [switch]$CheckBoard,
    [switch]$KeepBuild
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ProjectDir = (Resolve-Path $ProjectDir).Path

function Run-Step {
    param(
        [string]$Title,
        [scriptblock]$Body
    )

    Write-Host ""
    Write-Host "==== $Title ====" -ForegroundColor Cyan
    & $Body
}

Write-Host "==== 30TAI aim/follow acceptance preflight ====" -ForegroundColor Cyan
Write-Host "RepoRoot:   $RepoRoot"
Write-Host "ProjectDir: $ProjectDir"
Write-Host "BoardIp:    $BoardIp"
Write-Host "CheckBoard: $CheckBoard"

Run-Step "1. Local module build and unit test" {
    $args = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $RepoRoot "tools\run_local_aim_follow_checks.ps1")
    )
    if ($KeepBuild) {
        $args += "-KeepBuild"
    }
    & powershell @args
    if ($LASTEXITCODE -ne 0) {
        throw "Local aim/follow checks failed."
    }
}

Run-Step "2. Integrated PLin project check" {
    & powershell -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $RepoRoot "tools\verify_plin_integration.ps1") `
        -ProjectDir $ProjectDir
    if ($LASTEXITCODE -ne 0) {
        throw "PLin integration verification failed."
    }
}

Run-Step "3. 30TAI deploy dry run" {
    & powershell -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $RepoRoot "tools\check_deploy_dry_run.ps1") `
        -ProjectDir $ProjectDir
    if ($LASTEXITCODE -ne 0) {
        throw "Deploy dry run failed."
    }
}

if ($CheckBoard) {
    Run-Step "4. Board connection preflight" {
        & powershell -NoProfile -ExecutionPolicy Bypass `
            -File (Join-Path $RepoRoot "tools\check_30tai_connection.ps1") `
            -BoardIp $BoardIp
        if ($LASTEXITCODE -ne 0) {
            throw "Board connection preflight failed."
        }
    }
} else {
    Write-Host ""
    Write-Host "==== 4. Board connection preflight skipped ====" -ForegroundColor Yellow
    Write-Host "Run again with -CheckBoard after the 30TAI board is powered and connected."
}

Write-Host ""
Write-Host "Acceptance preflight finished." -ForegroundColor Green
Write-Host ""
Write-Host "Next real-board command after SSH is reachable:"
Write-Host "  powershell -ExecutionPolicy Bypass -File .\tools\deploy_30tai.ps1 -ProjectDir `"$ProjectDir`" -Build -SmokeTest -FetchLogs"
Write-Host ""
Write-Host "Then analyze fetched logs:"
Write-Host "  powershell -ExecutionPolicy Bypass -File .\tools\analyze_smoke_logs.ps1 -LogDir <FetchedSmokeLogDir>"
