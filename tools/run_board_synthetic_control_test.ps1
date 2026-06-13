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

function Utf8-Text {
    param([string]$Base64)
    return [System.Text.Encoding]::UTF8.GetString([System.Convert]::FromBase64String($Base64))
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
        $matchedLines = Select-String -Path $logPath -Pattern "\[SYNTH SUMMARY\]|\[SYNTH CAN 0x201\]|\[SYNTH CAN 0x38A\]"
        $matchedLines |
            Select-Object -Last 12 |
            ForEach-Object { $_.Line }

        $logText = Get-Content -Path $logPath -Raw
        $hasGimbalCan = $logText -match "\[SYNTH CAN 0x38A\]"
        $hasChassisCan = $logText -match "\[SYNTH CAN 0x201\]"
        $hasYawChange = $logText -match "yaw_change=1|yaw_change=true"
        $hasPitchChange = $logText -match "pitch_change=1|pitch_change=true"
        $hasForward = $logText -match "forward=1|forward=true"
        $hasBackward = $logText -match "backward=1|backward=true"
        $hasLostStop = $logText -match "lost_stop=1|lost_stop=true"

        $gimbalPass = $hasGimbalCan -and $hasYawChange -and $hasPitchChange
        $chassisPass = $hasChassisCan -and $hasForward -and $hasBackward -and $hasLostStop

        Write-Host ""
        Write-Host "[Tracking split result]" -ForegroundColor Cyan
        if ($gimbalPass) {
            Write-Host (Utf8-Text "5LqR5Y+w6L+96LiqOiBQQVNT77yM5bey55Sf5oiQIDB4MzhBIOS6keWPsCBDQU4g5bin77yM5LiUIHBpdGNoL3lhdyDkvJrpmo/nm67moIflgY/np7vlj5jljJbjgII=") -ForegroundColor Green
        } else {
            Write-Host (Utf8-Text "5LqR5Y+w6L+96LiqOiBGQUlM77yM6K+35qOA5p+lIDB4MzhBIOW4p+OAgXlhd19jaGFuZ2XjgIFwaXRjaF9jaGFuZ2XjgII=") -ForegroundColor Red
        }

        if ($chassisPass) {
            Write-Host (Utf8-Text "5bCP6L2m6L+96LiqOiBQQVNT77yM5bey55Sf5oiQIDB4MjAxIOWwj+i9piBDQU4g5bin77yM5LiU5YyF5ZCr6L+c6Led56a75YmN6L+b44CB6L+R6Led56a75ZCO6YCA44CB55uu5qCH5Lii5aSx5YGc5q2i6YC76L6R44CC") -ForegroundColor Green
        } else {
            Write-Host (Utf8-Text "5bCP6L2m6L+96LiqOiBGQUlM77yM6K+35qOA5p+lIDB4MjAxIOW4p+OAgWZvcndhcmTjgIFiYWNrd2FyZOOAgWxvc3Rfc3RvcOOAgg==") -ForegroundColor Red
        }

        if ($SendCan) {
            Write-Host (Utf8-Text "Q0FOIOWunuWPkTogRU5BQkxFRO+8jOacrOasoeS8muWwneivleWGmeWFpSBjYW4w77yb5pyq6L+e5o6l5bCP6L2m5pe25Y+v6IO95Zug5Li65pegIEFDSyDov5vlhaUgRVJST1ItUEFTU0lWReOAgg==") -ForegroundColor Yellow
        } else {
            Write-Host (Utf8-Text "Q0FOIOWunuWPkTogU0tJUFBFRO+8jOacrOasoeWPqumqjOivgeeul+azlei+k+WHuuWSjCBDQU4g6L296I2355Sf5oiQ77yM5LiN5ZCRIGNhbjAg55yf5a6e5Y+R6YCB44CC") -ForegroundColor Yellow
        }
    }
}
