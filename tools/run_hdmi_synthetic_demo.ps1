param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,
    [string]$BoardIp = "192.168.125.171",
    [string]$User = "root",
    [string]$SshKey = "",
    [int]$RunSeconds = 20,
    [switch]$SkipUpload,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

$args = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $RepoRoot "tools\run_board_vision_algorithm_test.ps1"),
    "-ProjectDir", (Resolve-Path $ProjectDir).Path,
    "-BoardIp", $BoardIp,
    "-User", $User,
    "-RunSeconds", "$RunSeconds",
    "-UseVtc",
    "-SyntheticTarget"
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

Write-Host "==== HDMI synthetic aim/follow demo ====" -ForegroundColor Cyan
Write-Host "This demo uses VTC + synthetic bicycle target + CAN dry-run."
Write-Host "HDMI should show distance, gimbal tracking, chassis tracking, and CAN output state."
Write-Host ""

& powershell @args
exit $LASTEXITCODE
