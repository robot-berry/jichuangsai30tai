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
$summaryLog = Join-Path $LogDir "summary.txt"

Write-Host "==== 30TAI vision/algorithm log analysis ====" -ForegroundColor Cyan
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

function Has-Match {
    param(
        [string]$Path,
        [string]$Pattern
    )
    if (-not (Test-Path $Path)) {
        return $false
    }
    return [bool](Select-String -LiteralPath $Path -Pattern $Pattern -SimpleMatch -Quiet)
}

$configCount = Count-Matches $appLog "[AIM FOLLOW CONFIG]"
$followCount = Count-Matches $appLog "[AIM FOLLOW]"
$distanceCount = Count-Matches $appLog "[DISTANCE DEBUG]"
$yoloDebugCount = Count-Matches $appLog "[YOLO DEBUG]"
$canDryRunCount = Count-Matches $appLog "DRYRUN id=0x"
$imageTimeout = Has-Match $appLog "ImageMake Timeout"
$zeroData = Has-Match $appLog "accept 0 data"
$dryRunEnabled = Has-Match $appLog "[CAN DRYRUN]"

Write-Host ""
Write-Host "Key counts:" -ForegroundColor Cyan
Write-Host "  AIM FOLLOW CONFIG: $configCount"
Write-Host "  AIM FOLLOW:        $followCount"
Write-Host "  DISTANCE DEBUG:    $distanceCount"
Write-Host "  YOLO DEBUG:        $yoloDebugCount"
Write-Host "  CAN DRYRUN frames: $canDryRunCount"

Write-Host ""
Write-Host "Checks:" -ForegroundColor Cyan
$checks = @(
    @{ Name = "CAN dry-run enabled"; Passed = $dryRunEnabled; Hint = "Missing [CAN DRYRUN]; check AIM_FOLLOW_CAN_DRYRUN=1." },
    @{ Name = "camera stream has no ImageMake timeout"; Passed = (-not $imageTimeout); Hint = "ImageMake Timeout usually means SDI input is not producing valid frames." },
    @{ Name = "camera stream has no zero-data accept"; Passed = (-not $zeroData); Hint = "accept 0 data usually means the SDI/camera path has no valid frame data." },
    @{ Name = "aim/follow config loaded"; Passed = ($configCount -gt 0); Hint = "Missing [AIM FOLLOW CONFIG]; deployed binary may not be rebuilt." },
    @{ Name = "target entered algorithm"; Passed = ($followCount -gt 0); Hint = "Missing [AIM FOLLOW]; YOLO may not detect the bicycle target." },
    @{ Name = "distance estimation visible"; Passed = ($distanceCount -gt 0); Hint = "Missing [DISTANCE DEBUG]; check detection box width and distance configuration." },
    @{ Name = "CAN output isolated"; Passed = ($canDryRunCount -gt 0); Hint = "Missing DRYRUN frame logs; algorithm may not be reaching command output." }
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
    Get-Content -LiteralPath $summaryLog -Tail 60
}

Write-Host ""
if ($failed -eq 0) {
    Write-Host "Vision/algorithm test passed without CAN control." -ForegroundColor Green
    exit 0
}

Write-Host "Vision/algorithm test found $failed issue(s)." -ForegroundColor Yellow
exit 1
