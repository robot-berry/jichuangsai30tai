param(
    [string]$BoardIp = "192.168.125.171",
    [string]$User = "root",
    [string]$SshKey = "",
    [string]$RemoteDir = "/tmp/aim_follow_synthetic_control",
    [string]$LocalLogDir = "",
    [switch]$SendCan,
    [switch]$ConfigureCan,
    [int]$Repeat = 1,
    [int]$SleepMs = 120,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ModuleDir = Join-Path $RepoRoot "aim_follow_control"
$SshTarget = "$User@$BoardIp"
$SshOptionArgs = @("-o", "ConnectTimeout=10", "-o", "StrictHostKeyChecking=no")
if (-not [string]::IsNullOrWhiteSpace($SshKey)) {
    $SshKey = (Resolve-Path $SshKey).Path
    $SshOptionArgs += @("-i", $SshKey)
}

if ([string]::IsNullOrWhiteSpace($LocalLogDir)) {
    $LocalLogDir = Join-Path $RepoRoot "board_synthetic_logs"
}

$ArchivePath = Join-Path $env:TEMP "aim_follow_control_synthetic.tar.gz"
$RemoteArchive = "/tmp/aim_follow_control_synthetic.tar.gz"
$RemoteLogDir = "/tmp/aim_follow_synthetic_logs_$(Get-Date -Format 'yyyyMMdd_HHmmss')"

function Run-Step {
    param(
        [string]$Title,
        [scriptblock]$Body,
        [string]$Preview
    )

    Write-Host ""
    Write-Host "[$Title]" -ForegroundColor Cyan
    if ($Preview) {
        Write-Host $Preview
    }
    if (-not $DryRun) {
        & $Body
    }
}

Write-Host "==== 30TAI synthetic aim/follow control test ====" -ForegroundColor Cyan
Write-Host "RepoRoot:     $RepoRoot"
Write-Host "Board:        $SshTarget"
Write-Host "RemoteDir:    $RemoteDir"
Write-Host "RemoteLogDir: $RemoteLogDir"
Write-Host "LocalLogDir:  $LocalLogDir"
Write-Host "SendCan:      $SendCan"
Write-Host "ConfigureCan: $ConfigureCan"
if (-not [string]::IsNullOrWhiteSpace($SshKey)) {
    Write-Host "SSH key:      $SshKey"
}

if (Test-Path $ArchivePath) {
    Remove-Item -LiteralPath $ArchivePath -Force
}

$tarArgs = @(
    "-czf", $ArchivePath,
    "--exclude=build",
    "--exclude=build*",
    "-C", $RepoRoot,
    "aim_follow_control"
)

Run-Step "Create archive" {
    & tar @tarArgs
} ("tar " + ($tarArgs -join " "))

Run-Step "Upload archive" {
    & scp @SshOptionArgs $ArchivePath "${SshTarget}:$RemoteArchive"
} "scp <archive> ${SshTarget}:$RemoteArchive"

$remoteSetup = "rm -rf '$RemoteDir' '$RemoteLogDir' && mkdir -p '$RemoteDir' '$RemoteLogDir' && tar --warning=no-timestamp -xzf '$RemoteArchive' -C '$RemoteDir' --strip-components=1 && find '$RemoteDir' -exec touch -h {} +"
Run-Step "Unpack on board" {
    & ssh @SshOptionArgs $SshTarget $remoteSetup
} "ssh $SshTarget `"$remoteSetup`""

$remoteBuild = "cd '$RemoteDir' && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j1"
Run-Step "Build on board" {
    & ssh @SshOptionArgs $SshTarget $remoteBuild
} "ssh $SshTarget `"$remoteBuild`""

$testArgs = @("--repeat", "$Repeat", "--sleep-ms", "$SleepMs")
if ($SendCan) {
    $testArgs += "--send-can"
}
if ($ConfigureCan) {
    $testArgs += "--configure-can"
}
$testArgString = $testArgs -join " "
$remoteRun = "cd '$RemoteDir' && ./build/aim_follow_synthetic_board_test $testArgString > '$RemoteLogDir/synthetic_control.log' 2>&1; ret=`$?; ip -details link show can0 > '$RemoteLogDir/can0_status_after.txt' 2>&1 || true; echo `$ret > '$RemoteLogDir/exit_code.txt'; exit `$ret"

Run-Step "Run synthetic control test" {
    & ssh @SshOptionArgs $SshTarget $remoteRun
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Synthetic control test returned nonzero; fetching logs for diagnosis." -ForegroundColor Yellow
    }
} "ssh $SshTarget `"$remoteRun`""

New-Item -ItemType Directory -Force -Path $LocalLogDir | Out-Null
$localRunDir = Join-Path $LocalLogDir (Split-Path $RemoteLogDir -Leaf)
Run-Step "Fetch logs" {
    & scp -r @SshOptionArgs "${SshTarget}:$RemoteLogDir" "$localRunDir"
} "scp -r ${SshTarget}:$RemoteLogDir $localRunDir"

if (-not $DryRun) {
    $logPath = Join-Path $localRunDir "synthetic_control.log"
    Write-Host ""
    Write-Host "Fetched logs to: $localRunDir" -ForegroundColor Green
    if (Test-Path $logPath) {
        Write-Host ""
        Write-Host "[Synthetic summary]" -ForegroundColor Cyan
        Select-String -Path $logPath -Pattern "\[SYNTH SUMMARY\]|\[SYNTH CAN 0x201\]|\[SYNTH CAN 0x38A\]" |
            Select-Object -Last 12 |
            ForEach-Object { $_.Line }
    }
}
