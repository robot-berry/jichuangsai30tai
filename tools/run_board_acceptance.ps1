param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,
    [string]$BoardIp = "192.168.125.171",
    [string]$User = "root",
    [string]$RemoteDir = "/home/fmsh/fpai_demo_src",
    [string]$RemoteSmokeLogDir = "/tmp/aim_follow_smoke",
    [string]$LocalLogDir = "",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ProjectDir = (Resolve-Path $ProjectDir).Path
if ([string]::IsNullOrWhiteSpace($LocalLogDir)) {
    $LocalLogDir = Join-Path $ProjectDir "board_smoke_logs"
}

function Run-Step {
    param(
        [string]$Title,
        [scriptblock]$Body
    )

    Write-Host ""
    Write-Host "==== $Title ====" -ForegroundColor Cyan
    & $Body
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

Write-Host "==== 30TAI real-board acceptance ====" -ForegroundColor Cyan
Write-Host "RepoRoot:     $RepoRoot"
Write-Host "ProjectDir:   $ProjectDir"
Write-Host "Board:        $User@$BoardIp"
Write-Host "RemoteDir:    $RemoteDir"
Write-Host "RemoteLogDir: $RemoteSmokeLogDir"
Write-Host "LocalLogDir:  $LocalLogDir"
Write-Host "DryRun:       $DryRun"

Run-Step "1. Preflight with board check" {
    $args = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $RepoRoot "tools\run_acceptance_preflight.ps1"),
        "-ProjectDir", $ProjectDir,
        "-BoardIp", $BoardIp,
        "-CheckBoard"
    )

    if ($DryRun) {
        Write-Host ("powershell " + ($args -join " "))
    } else {
        & powershell @args
        if ($LASTEXITCODE -ne 0) {
            throw "Acceptance preflight with board check failed."
        }
    }
}

$beforeLatest = Get-LatestLogDir -Root $LocalLogDir

Run-Step "2. Deploy, build, run smoke test, and fetch logs" {
    $args = @(
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
        "-SmokeTest",
        "-FetchLogs"
    )

    if ($DryRun) {
        $args += "-DryRun"
    }

    & powershell @args
    if ($LASTEXITCODE -ne 0) {
        throw "Board deploy/build/smoke/fetch failed."
    }
}

if ($DryRun) {
    Write-Host ""
    Write-Host "Dry run finished. Log analysis is skipped because no board logs were fetched." -ForegroundColor Yellow
    exit 0
}

$afterLatest = Get-LatestLogDir -Root $LocalLogDir
if (-not $afterLatest) {
    throw "No fetched smoke log directory found under: $LocalLogDir"
}

if ($beforeLatest -and ($afterLatest.FullName -eq $beforeLatest.FullName)) {
    Write-Host ""
    Write-Host "Warning: latest log directory did not change after fetch. The existing latest directory will be analyzed." -ForegroundColor Yellow
}

Run-Step "3. Analyze fetched smoke logs" {
    & powershell -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $RepoRoot "tools\analyze_smoke_logs.ps1") `
        -LogDir $afterLatest.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "Smoke log analysis failed."
    }
}

Run-Step "4. Write acceptance report" {
    & powershell -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $RepoRoot "tools\write_acceptance_report.ps1") `
        -LogDir $afterLatest.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "Acceptance report generation failed."
    }
}

Write-Host ""
Write-Host "30TAI real-board acceptance passed." -ForegroundColor Green
Write-Host "Analyzed log directory: $($afterLatest.FullName)"
Write-Host "Acceptance report: $(Join-Path $afterLatest.FullName 'acceptance_report.md')"
