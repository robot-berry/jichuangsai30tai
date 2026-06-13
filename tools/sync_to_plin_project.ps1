param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ProjectDir = (Resolve-Path $ProjectDir).Path

$sourceModule = Join-Path $RepoRoot "aim_follow_control"
$targetModule = Join-Path $ProjectDir "aim_follow_control"
$sourceTools = Join-Path $RepoRoot "tools"
$targetTools = Join-Path $ProjectDir "tools"
$cmakePath = Join-Path $ProjectDir "CMakeLists.txt"

if (-not (Test-Path $cmakePath)) {
    throw "CMakeLists.txt not found under ProjectDir: $ProjectDir"
}

function Copy-Directory {
    param(
        [string]$Source,
        [string]$Destination
    )
    Write-Host "Copy $Source -> $Destination"
    if (-not $DryRun) {
        if (Test-Path $Destination) {
            Remove-Item -LiteralPath $Destination -Recurse -Force
        }
        Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
    }
}

Write-Host "RepoRoot:   $RepoRoot"
Write-Host "ProjectDir: $ProjectDir"
Write-Host "DryRun:     $DryRun"

Copy-Directory -Source $sourceModule -Destination $targetModule

Write-Host "Ensure tools directory exists"
if (-not $DryRun) {
    New-Item -ItemType Directory -Force -Path $targetTools | Out-Null
}

$toolNames = @(
    "analyze_smoke_logs.ps1",
    "check_30tai_connection.ps1",
    "diagnose_30tai_camera_path.ps1",
    "diagnose_30tai_can_bus.ps1",
    "diagnose_30tai_video_input.ps1",
    "deploy_30tai.ps1",
    "dump_30tai_sdi_registers.ps1",
    "find_30tai_board.ps1",
    "probe_30tai_sdi_modes.ps1",
    "run_board_acceptance.ps1",
    "run_board_readiness_report.ps1",
    "run_board_synthetic_control_test.ps1",
    "run_acceptance_preflight.ps1",
    "verify_plin_integration.ps1"
)

foreach ($toolName in $toolNames) {
    $src = Join-Path $sourceTools $toolName
    $dst = Join-Path $targetTools $toolName
    Write-Host "Copy $toolName"
    if (-not $DryRun) {
        Copy-Item -LiteralPath $src -Destination $dst -Force
    }
}

$cmakeText = Get-Content -LiteralPath $cmakePath -Raw
$hasSource = $cmakeText.Contains("aim_follow_control/src/aim_follow_controller.cpp")
$hasInclude = $cmakeText.Contains("aim_follow_control/include")

Write-Host ""
Write-Host "CMake integration check:"
Write-Host "  source entry:  $hasSource"
Write-Host "  include entry: $hasInclude"

if (-not $hasSource -or -not $hasInclude) {
    Write-Host ""
    Write-Host "Manual CMake changes required:" -ForegroundColor Yellow
    if (-not $hasSource) {
        Write-Host "  Add source: aim_follow_control/src/aim_follow_controller.cpp"
    }
    if (-not $hasInclude) {
        Write-Host "  Add include: `${CMAKE_CURRENT_SOURCE_DIR}/aim_follow_control/include"
    }
} else {
    Write-Host "CMake already references aim_follow_control." -ForegroundColor Green
}

Write-Host ""
Write-Host "Sync finished."
