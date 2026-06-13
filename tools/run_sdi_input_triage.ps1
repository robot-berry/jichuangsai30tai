param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,
    [string]$BoardIp = "192.168.125.171",
    [string]$User = "root",
    [string]$SshKey = "",
    [int]$RunSeconds = 8,
    [switch]$SkipUpload,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ProjectDir = (Resolve-Path $ProjectDir).Path
$ReportRoot = Join-Path $ProjectDir "board_vision_algorithm_logs"
$RunId = Get-Date -Format "yyyyMMdd_HHmmss"
$TriageDir = Join-Path $ReportRoot "sdi_triage_$RunId"

New-Item -ItemType Directory -Force -Path $TriageDir | Out-Null

function Invoke-VisionTest {
    param(
        [string]$Name,
        [switch]$UseVtcMode
    )

    $logDir = Join-Path $TriageDir $Name
    $args = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $RepoRoot "tools\run_board_vision_algorithm_test.ps1"),
        "-ProjectDir", $ProjectDir,
        "-BoardIp", $BoardIp,
        "-User", $User,
        "-RunSeconds", "$RunSeconds",
        "-LocalLogDir", $logDir
    )
    if ($SshKey) {
        $args += @("-SshKey", (Resolve-Path $SshKey).Path)
    }
    if ($SkipUpload) {
        $args += "-SkipUpload"
    }
    if ($SkipBuild) {
        $args += "-SkipBuild"
    }
    if ($UseVtcMode) {
        $args += "-UseVtc"
    }

    Write-Host ""
    Write-Host "==== $Name ====" -ForegroundColor Cyan
    & powershell @args | ForEach-Object { Write-Host $_ }
    $exitCode = $LASTEXITCODE
    return $exitCode
}

function Get-LatestAppLog {
    param([string]$Root)
    if (-not (Test-Path $Root)) {
        return $null
    }
    return Get-ChildItem -LiteralPath $Root -Recurse -Filter "app.log" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}

function Count-Pattern {
    param(
        [string]$Path,
        [string]$Pattern
    )
    if (-not $Path -or -not (Test-Path $Path)) {
        return 0
    }
    return @(Select-String -LiteralPath $Path -Pattern $Pattern -SimpleMatch).Count
}

Write-Host "==== 30TAI SDI input triage ====" -ForegroundColor Cyan
Write-Host "ProjectDir: $ProjectDir"
Write-Host "Board:      $User@$BoardIp"
Write-Host "RunSeconds: $RunSeconds"
Write-Host "TriageDir:  $TriageDir"

$vtcRet = Invoke-VisionTest -Name "vtc_control" -UseVtcMode
$sdiRet = Invoke-VisionTest -Name "real_sdi"

$vtcLog = Get-LatestAppLog (Join-Path $TriageDir "vtc_control")
$sdiLog = Get-LatestAppLog (Join-Path $TriageDir "real_sdi")

$vtcTimeout = Count-Pattern $vtcLog.FullName "ImageMake Timeout"
$vtcZero = Count-Pattern $vtcLog.FullName "accept 0 data"
$sdiTimeout = Count-Pattern $sdiLog.FullName "ImageMake Timeout"
$sdiZero = Count-Pattern $sdiLog.FullName "accept 0 data"
$sdiAim = Count-Pattern $sdiLog.FullName "[AIM FOLLOW]"
$sdiDistance = Count-Pattern $sdiLog.FullName "[DISTANCE DEBUG]"

$vtcFrameReady = ($vtcTimeout -eq 0) -and ($vtcZero -eq 0)
$sdiFrameReady = ($sdiTimeout -eq 0) -and ($sdiZero -eq 0)

$reportPath = Join-Path $TriageDir "sdi_input_triage_report.md"
$report = @()
$report += "# 30TAI SDI Input Triage Report"
$report += ""
$report += "- Run time: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$report += "- Board: ``$User@$BoardIp``"
$report += "- Project: ``$ProjectDir``"
$report += "- VTC app log: ``$($vtcLog.FullName)``"
$report += "- Real SDI app log: ``$($sdiLog.FullName)``"
$report += ""
$report += "| Gate | Result | Evidence |"
$report += "| --- | --- | --- |"
$report += "| VTC/internal frame path | $(if ($vtcFrameReady) { 'PASS' } else { 'FAIL' }) | ImageMake Timeout=$vtcTimeout, accept 0 data=$vtcZero, exit=$vtcRet |"
$report += "| Real SDI frame path | $(if ($sdiFrameReady) { 'PASS' } else { 'FAIL' }) | ImageMake Timeout=$sdiTimeout, accept 0 data=$sdiZero, exit=$sdiRet |"
$report += "| Real target algorithm logs | $(if ($sdiAim -gt 0 -and $sdiDistance -gt 0) { 'PASS' } elseif ($sdiFrameReady) { 'NO_TARGET' } else { 'SKIPPED_NO_FRAME' }) | AIM FOLLOW=$sdiAim, DISTANCE DEBUG=$sdiDistance |"
$report += ""
$report += "## Conclusion"
$report += ""
if ($vtcFrameReady -and -not $sdiFrameReady) {
    $report += "- The application/model/HDMI/dry-run path starts with the internal VTC source, but the physical SDI path has no valid frame input."
    $report += "- Focus next on camera power, SDI cable, SDI_IN_0 signal, camera output format, and the loaded bitstream's external SDI input support."
} elseif ($vtcFrameReady -and $sdiFrameReady) {
    $report += "- Both VTC and real SDI frame paths are producing data. Place the bicycle target in view and check `[AIM FOLLOW]`, `[DISTANCE DEBUG]`, and the HDMI panel."
} elseif (-not $vtcFrameReady) {
    $report += "- Even the internal VTC source failed, so inspect the application/model/bitstream/runtime setup before debugging the physical camera."
}

$report | Set-Content -LiteralPath $reportPath -Encoding UTF8

Write-Host ""
Write-Host "SDI triage report written:" -ForegroundColor Green
Write-Host $reportPath
Write-Host ""
Get-Content -Raw -LiteralPath $reportPath

if ($vtcFrameReady -and -not $sdiFrameReady) {
    exit 2
}
if (-not $vtcFrameReady) {
    exit 1
}
exit 0
