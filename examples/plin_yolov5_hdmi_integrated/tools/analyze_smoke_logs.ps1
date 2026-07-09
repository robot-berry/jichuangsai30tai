param(
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $defaultRoot = Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")).Path "board_smoke_logs"
    if (-not (Test-Path $defaultRoot)) {
        throw "LogDir is required, and default log root does not exist: $defaultRoot"
    }
    $latest = Get-ChildItem -LiteralPath $defaultRoot -Directory |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $latest) {
        throw "No smoke log directory found under: $defaultRoot"
    }
    $LogDir = $latest.FullName
} else {
    $LogDir = (Resolve-Path $LogDir).Path
}

$appLog = Join-Path $LogDir "app.log"
$canLog = Join-Path $LogDir "candump.log"
$summaryLog = Join-Path $LogDir "summary.txt"

Write-Host "==== 30TAI smoke log analysis ====" -ForegroundColor Cyan
Write-Host "LogDir: $LogDir"

function Count-Matches {
    param(
        [string]$Path,
        [string]$Pattern
    )
    if (-not (Test-Path $Path)) {
        return 0
    }
    return @(Select-String -LiteralPath $Path -Pattern $Pattern -SimpleMatch).Count
}

$configCount = Count-Matches $appLog "[AIM FOLLOW CONFIG]"
$followCount = Count-Matches $appLog "[AIM FOLLOW]"
$distanceCount = Count-Matches $appLog "[DISTANCE DEBUG]"
$yoloDebugCount = Count-Matches $appLog "[YOLO DEBUG]"
$chassisCount = Count-Matches $canLog " 201 "
$gimbalCount = Count-Matches $canLog " 38A "

Write-Host ""
Write-Host "Key counts:" -ForegroundColor Cyan
Write-Host "  AIM FOLLOW CONFIG: $configCount"
Write-Host "  AIM FOLLOW:        $followCount"
Write-Host "  DISTANCE DEBUG:    $distanceCount"
Write-Host "  YOLO DEBUG:        $yoloDebugCount"
Write-Host "  CAN 0x201:         $chassisCount"
Write-Host "  CAN 0x38A:         $gimbalCount"

Write-Host ""
Write-Host "Checks:" -ForegroundColor Cyan
$checks = @(
    @{ Name = "config loaded"; Passed = ($configCount -gt 0); Hint = "Missing [AIM FOLLOW CONFIG]; check whether the deployed binary is rebuilt." },
    @{ Name = "target entered aim/follow control"; Passed = ($followCount -gt 0); Hint = "Missing [AIM FOLLOW]; check whether YOLO detects bicycle." },
    @{ Name = "distance estimation visible"; Passed = ($distanceCount -gt 0); Hint = "Missing [DISTANCE DEBUG]; check box width and distance estimation." },
    @{ Name = "chassis CAN frame exists"; Passed = ($chassisCount -gt 0); Hint = "Missing 0x201; check can0, CAN_BITRATE, and send_chassis_can_mode." },
    @{ Name = "gimbal CAN frame exists"; Passed = ($gimbalCount -gt 0); Hint = "Missing 0x38A; check can0, CAN_BITRATE, and send_gimbal_can_mode." }
)

$failed = 0
foreach ($check in $checks) {
    if ($check.Passed) {
        Write-Host "  [PASS] $($check.Name)" -ForegroundColor Green
    } else {
        ++$failed
        Write-Host "  [FAIL] $($check.Name) - $($check.Hint)" -ForegroundColor Yellow
    }
}

if (Test-Path $summaryLog) {
    Write-Host ""
    Write-Host "Summary tail:" -ForegroundColor Cyan
    Get-Content -LiteralPath $summaryLog -Tail 40
}

Write-Host ""
if ($failed -eq 0) {
    Write-Host "Smoke log analysis passed." -ForegroundColor Green
    exit 0
}

Write-Host "Smoke log analysis found $failed issue(s)." -ForegroundColor Yellow
exit 1
