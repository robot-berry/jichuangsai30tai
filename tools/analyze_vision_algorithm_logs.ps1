param(
    [string]$LogDir = "",
    [switch]$AllowNoTarget
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
Write-Host "AllowNoTarget: $AllowNoTarget"

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
$frameInputReady = (-not $imageTimeout) -and (-not $zeroData)

Write-Host ""
Write-Host "Key counts:" -ForegroundColor Cyan
Write-Host "  AIM FOLLOW CONFIG: $configCount"
Write-Host "  AIM FOLLOW:        $followCount"
Write-Host "  DISTANCE DEBUG:    $distanceCount"
Write-Host "  YOLO DEBUG:        $yoloDebugCount"
Write-Host "  CAN DRYRUN frames: $canDryRunCount"

Write-Host ""
Write-Host "Checks:" -ForegroundColor Cyan
$failed = 0
function Write-Check {
    param(
        [string]$Name,
        [bool]$Passed,
        [string]$Hint
    )
    if ($Passed) {
        Write-Host "  [PASS] $Name" -ForegroundColor Green
        return
    }
    $script:failed += 1
    Write-Host "  [FAIL] $Name - $Hint" -ForegroundColor Yellow
}

function Write-Skip {
    param(
        [string]$Name,
        [string]$Reason
    )
    Write-Host "  [SKIP] $Name - $Reason" -ForegroundColor DarkYellow
}

Write-Check "CAN dry-run enabled" $dryRunEnabled "Missing [CAN DRYRUN]; check AIM_FOLLOW_CAN_DRYRUN=1."
Write-Check "camera stream has no ImageMake timeout" (-not $imageTimeout) "ImageMake Timeout usually means SDI input is not producing valid frames."
Write-Check "camera stream has no zero-data accept" (-not $zeroData) "accept 0 data usually means the SDI/camera path has no valid frame data."
Write-Check "aim/follow config loaded" ($configCount -gt 0) "Missing [AIM FOLLOW CONFIG]; deployed binary may not be rebuilt."

if ($frameInputReady) {
    if ($AllowNoTarget -and $followCount -eq 0) {
        Write-Skip "target entered algorithm" "no target required for this frame-path test"
        Write-Skip "distance estimation visible" "no target required for this frame-path test"
    } else {
        Write-Check "target entered algorithm" ($followCount -gt 0) "Missing [AIM FOLLOW]; YOLO may not detect the bicycle target."
        Write-Check "distance estimation visible" ($distanceCount -gt 0) "Missing [DISTANCE DEBUG]; check detection box width and distance configuration."
    }
    Write-Check "CAN output isolated" ($canDryRunCount -gt 0) "Missing DRYRUN frame logs; algorithm may not be reaching command output."
} else {
    Write-Skip "target entered algorithm" "no valid camera frame reached ImageMake/model pipeline"
    Write-Skip "distance estimation visible" "distance requires a detected target box"
    Write-Skip "CAN output isolated after target" "command output requires a valid target or target-lost loop after frame processing"
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
