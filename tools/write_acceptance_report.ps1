param(
    [Parameter(Mandatory = $true)]
    [string]$LogDir,
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"

$LogDir = (Resolve-Path $LogDir).Path
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $LogDir "acceptance_report.md"
}

$appLog = Join-Path $LogDir "app.log"
$canLog = Join-Path $LogDir "candump.log"
$summaryLog = Join-Path $LogDir "summary.txt"

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

function Get-FirstMatches {
    param(
        [string]$Path,
        [string]$Pattern,
        [int]$Count = 5
    )
    if (-not (Test-Path $Path)) {
        return @()
    }
    return @(Select-String -LiteralPath $Path -Pattern $Pattern -SimpleMatch |
        Select-Object -First $Count |
        ForEach-Object { $_.Line })
}

$configCount = Count-Matches $appLog "[AIM FOLLOW CONFIG]"
$followCount = Count-Matches $appLog "[AIM FOLLOW]"
$distanceCount = Count-Matches $appLog "[DISTANCE DEBUG]"
$yoloDebugCount = Count-Matches $appLog "[YOLO DEBUG]"
$chassisCount = Count-Matches $canLog " 201 "
$gimbalCount = Count-Matches $canLog " 38A "

$checks = @(
    [pscustomobject]@{ Name = "Aim/follow config loaded"; Evidence = "[AIM FOLLOW CONFIG]"; Count = $configCount; Passed = ($configCount -gt 0) },
    [pscustomobject]@{ Name = "Target entered aim/follow control"; Evidence = "[AIM FOLLOW]"; Count = $followCount; Passed = ($followCount -gt 0) },
    [pscustomobject]@{ Name = "Distance display/debug active"; Evidence = "[DISTANCE DEBUG]"; Count = $distanceCount; Passed = ($distanceCount -gt 0) },
    [pscustomobject]@{ Name = "Chassis CAN command emitted"; Evidence = "CAN ID 0x201"; Count = $chassisCount; Passed = ($chassisCount -gt 0) },
    [pscustomobject]@{ Name = "Gimbal CAN command emitted"; Evidence = "CAN ID 0x38A"; Count = $gimbalCount; Passed = ($gimbalCount -gt 0) }
)

$passedCount = @($checks | Where-Object { $_.Passed }).Count
$failedCount = $checks.Count - $passedCount
$result = if ($failedCount -eq 0) { "PASS" } else { "FAIL" }
$timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"

$configSamples = Get-FirstMatches $appLog "[AIM FOLLOW CONFIG]" 3
$followSamples = Get-FirstMatches $appLog "[AIM FOLLOW]" 5
$distanceSamples = Get-FirstMatches $appLog "[DISTANCE DEBUG]" 5
$chassisSamples = Get-FirstMatches $canLog " 201 " 5
$gimbalSamples = Get-FirstMatches $canLog " 38A " 5

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# 30TAI Aim/Follow Acceptance Report")
$lines.Add("")
$lines.Add("- Generated: $timestamp")
$lines.Add("- Log directory: ``$LogDir``")
$lines.Add("- Result: **$result**")
$lines.Add("")
$lines.Add("## Evidence Summary")
$lines.Add("")
$lines.Add("| Check | Evidence | Count | Result |")
$lines.Add("| --- | --- | ---: | --- |")
foreach ($check in $checks) {
    $checkResult = if ($check.Passed) { "PASS" } else { "FAIL" }
    $lines.Add("| $($check.Name) | ``$($check.Evidence)`` | $($check.Count) | $checkResult |")
}
$lines.Add("")
$lines.Add("## Key Counts")
$lines.Add("")
$lines.Add("- AIM FOLLOW CONFIG: $configCount")
$lines.Add("- AIM FOLLOW: $followCount")
$lines.Add("- DISTANCE DEBUG: $distanceCount")
$lines.Add("- YOLO DEBUG: $yoloDebugCount")
$lines.Add("- CAN 0x201: $chassisCount")
$lines.Add("- CAN 0x38A: $gimbalCount")
$lines.Add("")

function Add-SampleSection {
    param(
        [string]$Title,
        [string[]]$Samples
    )
    $lines.Add("## $Title")
    $lines.Add("")
    if ($Samples.Count -eq 0) {
        $lines.Add("_No sample lines found._")
        $lines.Add("")
        return
    }
    $lines.Add('```text')
    foreach ($sample in $Samples) {
        $lines.Add($sample)
    }
    $lines.Add('```')
    $lines.Add("")
}

Add-SampleSection "Aim/Follow Config Samples" $configSamples
Add-SampleSection "Aim/Follow Runtime Samples" $followSamples
Add-SampleSection "Distance Debug Samples" $distanceSamples
Add-SampleSection "Chassis CAN 0x201 Samples" $chassisSamples
Add-SampleSection "Gimbal CAN 0x38A Samples" $gimbalSamples

if (Test-Path $summaryLog) {
    $lines.Add("## Smoke Summary Tail")
    $lines.Add("")
    $lines.Add('```text')
    foreach ($line in (Get-Content -LiteralPath $summaryLog -Tail 40)) {
        $lines.Add($line)
    }
    $lines.Add('```')
    $lines.Add("")
}

$lines.Add("## Acceptance Notes")
$lines.Add("")
if ($failedCount -eq 0) {
    $lines.Add("The fetched smoke logs prove that the integrated application loaded the aim/follow module, produced distance debug output, and emitted chassis/gimbal CAN frames during the smoke test.")
} else {
    $lines.Add("One or more required evidence items are missing. Inspect the failed checks above before treating this board run as accepted.")
}

$outputDir = Split-Path $OutputPath -Parent
if (-not [string]::IsNullOrWhiteSpace($outputDir)) {
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
}

Set-Content -LiteralPath $OutputPath -Value $lines -Encoding UTF8

Write-Host "Acceptance report written: $OutputPath"
Write-Host "Result: $result"

if ($failedCount -ne 0) {
    exit 1
}
