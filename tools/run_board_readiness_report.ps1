param(
    [string]$BoardIp = "192.168.125.171",
    [string]$User = "root",
    [string]$SshKey = "",
    [string]$ProjectDir = "",
    [string]$RemoteDir = "/home/work/fpai_demo_app/examples/codex/fpai_demo_src",
    [string]$OriginalDir = "/home/work/fpai_demo_app/examples/1_single_input+ai/PLin+SingleNet+HDMI",
    [string]$LocalReportDir = "",
    [int]$VideoRunSeconds = 8,
    [int]$CanObserveSeconds = 3,
    [switch]$SkipVideo,
    [switch]$SkipSynthetic,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($LocalReportDir)) {
    $LocalReportDir = Join-Path $RepoRoot "board_readiness_reports"
}

if (-not [string]::IsNullOrWhiteSpace($SshKey)) {
    $SshKey = (Resolve-Path $SshKey).Path
}

$RunId = Get-Date -Format "yyyyMMdd_HHmmss"
$RunReportDir = Join-Path $LocalReportDir "readiness_$RunId"
$CanLogRoot = Join-Path $RunReportDir "can"
$VideoLogRoot = Join-Path $RunReportDir "video"
$SyntheticLogRoot = Join-Path $RunReportDir "synthetic"

function Invoke-Tool {
    param(
        [string]$Title,
        [string[]]$ToolArgs
    )

    Write-Host ""
    Write-Host "==== $Title ====" -ForegroundColor Cyan
    Write-Host ("powershell " + ($ToolArgs -join " "))
    if (-not $DryRun) {
        & powershell @ToolArgs | ForEach-Object { Write-Host $_ }
        $exitCode = $LASTEXITCODE
        return $exitCode
    }
    return 0
}

function Get-LatestDir {
    param([string]$Root)
    if (-not (Test-Path $Root)) {
        return $null
    }
    return Get-ChildItem -LiteralPath $Root -Directory |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}

function Test-FileContains {
    param(
        [string]$Path,
        [string]$Pattern
    )
    if (-not (Test-Path $Path)) {
        return $false
    }
    return [bool](Select-String -Path $Path -Pattern $Pattern -Quiet)
}

Write-Host "==== 30TAI readiness report ====" -ForegroundColor Cyan
Write-Host "RepoRoot:   $RepoRoot"
Write-Host "Board:      $User@$BoardIp"
Write-Host "ReportDir:  $RunReportDir"
Write-Host "RemoteDir:  $RemoteDir"
Write-Host "SkipVideo:  $SkipVideo"
Write-Host "SkipSynth:  $SkipSynthetic"
Write-Host "DryRun:     $DryRun"
if ($SshKey) {
    Write-Host "SSH key:    $SshKey"
}

if (-not $DryRun) {
    New-Item -ItemType Directory -Force -Path $CanLogRoot, $VideoLogRoot, $SyntheticLogRoot | Out-Null
}

$commonArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass"
)

$connectionArgs = $commonArgs + @(
    "-File", (Join-Path $RepoRoot "tools\check_30tai_connection.ps1"),
    "-BoardIp", $BoardIp,
    "-User", $User
)
if ($SshKey) {
    $connectionArgs += @("-SshKey", $SshKey)
}
$connectionRet = Invoke-Tool "Connection check" $connectionArgs

$canArgs = $commonArgs + @(
    "-File", (Join-Path $RepoRoot "tools\diagnose_30tai_can_bus.ps1"),
    "-BoardIp", $BoardIp,
    "-User", $User,
    "-ObserveSeconds", "$CanObserveSeconds",
    "-LocalLogDir", $CanLogRoot
)
if ($SshKey) {
    $canArgs += @("-SshKey", $SshKey)
}
$canRet = Invoke-Tool "CAN diagnostic" $canArgs

$syntheticRet = 0
if (-not $SkipSynthetic) {
    $syntheticArgs = $commonArgs + @(
        "-File", (Join-Path $RepoRoot "tools\run_board_synthetic_control_test.ps1"),
        "-BoardIp", $BoardIp,
        "-User", $User,
        "-LocalLogDir", $SyntheticLogRoot,
        "-Repeat", "1",
        "-SleepMs", "10"
    )
    if ($SshKey) {
        $syntheticArgs += @("-SshKey", $SshKey)
    }
    $syntheticRet = Invoke-Tool "Synthetic controller test" $syntheticArgs
}

$videoRet = 0
if (-not $SkipVideo) {
    $videoArgs = $commonArgs + @(
        "-File", (Join-Path $RepoRoot "tools\diagnose_30tai_video_input.ps1"),
        "-BoardIp", $BoardIp,
        "-User", $User,
        "-RemoteDir", $RemoteDir,
        "-OriginalDir", $OriginalDir,
        "-RunSeconds", "$VideoRunSeconds",
        "-LocalLogDir", $VideoLogRoot
    )
    if ($SshKey) {
        $videoArgs += @("-SshKey", $SshKey)
    }
    $videoRet = Invoke-Tool "Video input diagnostic" $videoArgs
}

if ($DryRun) {
    Write-Host ""
    Write-Host "Dry run finished. No report was written." -ForegroundColor Yellow
    exit 0
}

$canDir = Get-LatestDir $CanLogRoot
$syntheticDir = Get-LatestDir $SyntheticLogRoot
$videoDir = Get-LatestDir $VideoLogRoot

$canSummary = if ($canDir) { Join-Path $canDir.FullName "summary.md" } else { "" }
$syntheticLog = if ($syntheticDir) { Join-Path $syntheticDir.FullName "synthetic_control.log" } else { "" }
$videoSummary = if ($videoDir) { Join-Path $videoDir.FullName "summary.md" } else { "" }

$canReady = Test-FileContains $canSummary "Result: CAN controller is active"
$syntheticPass = (-not $SkipSynthetic) -and (Test-FileContains $syntheticLog "\[SYNTH SUMMARY\].*result=PASS")
$videoReady = (-not $SkipVideo) -and (
    (Test-Path $videoSummary) -and
    -not (Test-FileContains $videoSummary "accept 0 data") -and
    -not (Test-FileContains $videoSummary "real SDI input loss")
)

$overallReady = ($connectionRet -eq 0) -and ($canRet -eq 0) -and ($syntheticRet -eq 0) -and ($videoRet -eq 0) -and $canReady -and $syntheticPass -and $videoReady

$reportPath = Join-Path $RunReportDir "readiness_report.md"
$readyText = if ($overallReady) { "YES" } else { "NO" }
$connectionText = if ($connectionRet -eq 0) { "PASS" } else { "FAIL" }
$canText = if ($canReady) { "PASS" } else { "FAIL" }
$syntheticText = if ($SkipSynthetic) { "SKIPPED" } elseif ($syntheticPass) { "PASS" } else { "FAIL" }
$videoText = if ($SkipVideo) { "SKIPPED" } elseif ($videoReady) { "PASS" } else { "FAIL" }
$syntheticDirText = if ($syntheticDir) { $syntheticDir.FullName } else { "skipped" }
$videoDirText = if ($videoDir) { $videoDir.FullName } else { "skipped" }

$report = @()
$report += "# 30TAI Aim/Follow Readiness Report"
$report += ""
$report += "- Run time: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$report += "- Board: ``$User@$BoardIp``"
$report += "- Remote PLin dir: ``$RemoteDir``"
$report += "- Overall ready for full closed-loop test: **$readyText**"
$report += ""
$report += "| Gate | Result | Evidence |"
$report += "| --- | --- | --- |"
$report += "| SSH connection | $connectionText | ``check_30tai_connection.ps1`` exit ``$connectionRet`` |"
$report += "| CAN bus healthy | $canText | ``$canSummary`` |"
$report += "| Controller synthetic behavior | $syntheticText | ``$syntheticLog`` |"
$report += "| Real video input | $videoText | ``$videoSummary`` |"
$report += ""
$report += "## Interpretation"
$report += ""
if (-not $canReady) {
    $report += "- CAN is not ready for motion commands. Do not use `-SendCan` until `diagnose_30tai_can_bus.ps1` reports `ERROR-ACTIVE`."
}
if ((-not $SkipVideo) -and (-not $videoReady)) {
    $report += "- Real SDI/video input is not ready for closed-loop target following. Fix the camera/SDI/bitstream path before judging YOLO-driven aim/follow."
}
if ((-not $SkipSynthetic) -and $syntheticPass) {
    $report += "- The aim/follow controller logic passes on 30TAI with synthetic target observations."
}
if ($SkipSynthetic -or $SkipVideo) {
    $report += "- One or more gates were skipped, so this report is not sufficient to approve full closed-loop testing."
}
if ($overallReady) {
    $report += "- All gates passed. Continue with lifted-wheel target-visible tests."
}
$report += ""
$report += "## Log Directories"
$report += ""
$report += "- CAN: ``$($canDir.FullName)``"
$report += "- Synthetic: ``$syntheticDirText``"
$report += "- Video: ``$videoDirText``"

$report | Set-Content -LiteralPath $reportPath -Encoding UTF8

Write-Host ""
Write-Host "Readiness report written:" -ForegroundColor Green
Write-Host $reportPath
Write-Host ""
Get-Content -Raw $reportPath

if (-not $overallReady) {
    exit 1
}
