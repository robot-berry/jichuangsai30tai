param(
    [string]$BoardIp = "192.168.125.171",
    [string]$BoardUser = "root",
    [string]$BoardPassword = "",
    [string]$RemoteDir = "/home/fmsh/plin_pHdmi/examples/codex/plin_main_current",
    [ValidateRange(0, 600)]
    [int]$BoardWaitSeconds = 180,
    [int]$PreviewPort = 8765,
    [double]$IrGain = 4.0,
    [int]$IrRedMin = 180,
    [int]$IrRedDominance = 50,
    [int]$IrReflectionMax = 200,
    [int]$IrLocalContrast = 8,
    [string]$IrReference = "",
    [switch]$PreviewOnly,
    [switch]$ArmChassis,
    [switch]$NoOpenBrowser
)

$ErrorActionPreference = "Stop"
$ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Target = "$BoardUser@$BoardIp"
$OutDir = Join-Path $ProjectDir "runtime\network_preview"
$Python = Join-Path $env:USERPROFILE ".cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if (-not (Test-Path $Python)) { $Python = "python" }
$EnableChassis = -not $PreviewOnly
if ($PreviewOnly -and $ArmChassis) {
    throw "-PreviewOnly and -ArmChassis cannot be used together."
}

function Find-SshTool([string]$Name) {
    $Command = Get-Command "$Name.exe" -ErrorAction SilentlyContinue
    if ($Command) { return $Command.Source }

    $Candidates = @(
        (Join-Path $env:SystemRoot "System32\OpenSSH\$Name.exe"),
        (Join-Path $env:USERPROFILE ".cache\codex-runtimes\codex-primary-runtime\dependencies\native\git\usr\bin\$Name.exe")
    )
    foreach ($Candidate in $Candidates) {
        if (Test-Path $Candidate) { return $Candidate }
    }
    throw "Missing $Name. Install Windows OpenSSH or Git for Windows."
}

$Ssh = Find-SshTool "ssh"
$Scp = Find-SshTool "scp"

if ([string]::IsNullOrWhiteSpace($BoardPassword)) {
    if (-not [string]::IsNullOrWhiteSpace($env:BOARD_PASS)) {
        $BoardPassword = $env:BOARD_PASS
    } else {
        $SecurePassword = Read-Host "Board SSH password" -AsSecureString
        $Ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecurePassword)
        try {
            $BoardPassword = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($Ptr)
        } finally {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($Ptr)
        }
    }
}

$Required = @(
    "prebuilt\ZG\sdicamera+yolov5+hdmi",
    "configs\ZG\sdicamera+yolov5+hdmi.yaml",
    "imodel\ZG\yolov5s_plin_352x640_ZG.json",
    "imodel\ZG\yolov5s_plin_352x640_ZG.raw",
    "names\coco.names",
    "run_30tai_3331.sh",
    "start_chassis_tracking_test.sh",
    "start_laser_aim_test.sh",
    "start_vision_dryrun.sh",
    "start_tracking_test.sh",
    "stop_all.sh",
    "tools\safe_can_control_session.py",
    "tools\safe_can_control_client.py",
    "tools\safe_tracking_bridge.py",
    "tools\start_autonomous_tracking.sh",
    "tools\stop_autonomous_tracking.sh",
    "tools\stream_plin_hdmi_udma.py",
    "tools\preview_plin_network_frames.py"
)
foreach ($RelativePath in $Required) {
    if (-not (Test-Path (Join-Path $ProjectDir $RelativePath))) {
        throw "Missing required file: $RelativePath"
    }
}

$SessionId = [Guid]::NewGuid().ToString("N")
$StageDir = Join-Path $env:TEMP "plin_autonomous_stage_$SessionId"
$Archive = Join-Path $env:TEMP "plin_autonomous_$SessionId.tar.gz"
$AskPass = Join-Path $env:TEMP "plin_askpass_$SessionId.cmd"
$PreviewAskPass = Join-Path $ProjectDir "runtime\preview_ssh_askpass.cmd"
$RemoteArchive = "/tmp/plin_autonomous_$SessionId.tar.gz"
$BoardStarted = $false
$SshArgs = @(
    "-o", "ConnectTimeout=10",
    "-o", "LogLevel=ERROR",
    "-o", "StrictHostKeyChecking=no",
    "-o", "UserKnownHostsFile=/dev/null",
    "-o", "PreferredAuthentications=password",
    "-o", "PubkeyAuthentication=no"
)

try {
    New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
    foreach ($Dir in @("build\ZG", "configs\ZG", "imodel\ZG", "names", "tools")) {
        New-Item -ItemType Directory -Force -Path (Join-Path $StageDir $Dir) | Out-Null
    }

    Copy-Item (Join-Path $ProjectDir "prebuilt\ZG\sdicamera+yolov5+hdmi") (Join-Path $StageDir "build\ZG")
    foreach ($RelativePath in $Required | Where-Object { $_ -notlike "prebuilt*" }) {
        $Destination = Join-Path $StageDir (Split-Path $RelativePath -Parent)
        if ([string]::IsNullOrEmpty($Destination)) { $Destination = $StageDir }
        Copy-Item (Join-Path $ProjectDir $RelativePath) $Destination -Force
    }

    & tar -czf $Archive -C $StageDir .
    if ($LASTEXITCODE -ne 0) { throw "Failed to create deployment archive." }

    [IO.File]::WriteAllText($AskPass, "@echo off`r`necho %BOARD_PASS%`r`n", [Text.Encoding]::ASCII)
    $env:BOARD_PASS = $BoardPassword
    $env:SSH_ASKPASS = $AskPass
    $env:SSH_ASKPASS_REQUIRE = "force"
    $env:DISPLAY = "codex"

    $ProbeArgs = @(
        "-o", "ConnectTimeout=2",
        "-o", "LogLevel=ERROR",
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=/dev/null",
        "-o", "PreferredAuthentications=password",
        "-o", "PubkeyAuthentication=no"
    )
    $BoardDeadline = [DateTime]::UtcNow.AddSeconds($BoardWaitSeconds)
    do {
        $ProbeExitCode = 1
        try {
            & $Ssh @ProbeArgs $Target "true" 2>$null
            $ProbeExitCode = $LASTEXITCODE
        } catch {
            $ProbeExitCode = 1
        }
        if ($ProbeExitCode -eq 0) { break }
        if ([DateTime]::UtcNow -ge $BoardDeadline) {
            throw "Board SSH did not become ready within $BoardWaitSeconds seconds."
        }
        Write-Host "Waiting for board SSH at $BoardIp..." -ForegroundColor Yellow
        Start-Sleep -Seconds 2
    } while ($true)

    & $Ssh @SshArgs $Target "if [ -x '$RemoteDir/stop_all.sh' ]; then '$RemoteDir/stop_all.sh'; fi; rm -rf '$RemoteDir'; mkdir -p '$RemoteDir'"
    if ($LASTEXITCODE -ne 0) { throw "Board preparation failed." }
    & $Scp @SshArgs $Archive "${Target}:$RemoteArchive"
    if ($LASTEXITCODE -ne 0) { throw "Board upload failed." }

    $RemoteStart = "set -e; tar -xzf '$RemoteArchive' -C '$RemoteDir'; chmod 755 '$RemoteDir/build/ZG/sdicamera+yolov5+hdmi' '$RemoteDir'/*.sh '$RemoteDir'/tools/*.sh '$RemoteDir'/tools/*.py; '$RemoteDir/start_vision_dryrun.sh'"
    if ($EnableChassis) {
        $RemoteStart += "; sleep 8; if ! REMOTE_DIR='$RemoteDir' '$RemoteDir/tools/start_autonomous_tracking.sh'; then '$RemoteDir/stop_all.sh'; exit 1; fi"
    }
    & $Ssh @SshArgs $Target $RemoteStart
    if ($LASTEXITCODE -ne 0) { throw "Board runtime failed to start." }
    $BoardStarted = $true

    Get-CimInstance Win32_Process | Where-Object {
        ($_.Name -eq "python.exe" -and $_.CommandLine -match "preview_plin_network_frames.py") -or
        ($_.Name -eq "ssh.exe" -and $_.CommandLine -match "stream_plin_hdmi_udma.py")
    } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 500

    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    Remove-Item -Path (Join-Path $OutDir "live_frame_*.jpg"), `
        (Join-Path $OutDir "live_enhanced_*.jpg"), `
        (Join-Path $OutDir "live_status.txt") -Force -ErrorAction SilentlyContinue
    $PreviewStdout = Join-Path $ProjectDir "runtime\preview_stdout.log"
    $PreviewStderr = Join-Path $ProjectDir "runtime\preview_stderr.log"
    [IO.File]::WriteAllText(
        $PreviewAskPass,
        "@echo off`r`necho %BOARD_PASS%`r`n",
        [Text.Encoding]::ASCII)
    $env:SSH_ASKPASS = $PreviewAskPass
    $PreviewArgs = @(
        (Join-Path $ProjectDir "tools\preview_plin_network_frames.py"),
        "--ssh", $Ssh,
        "--target", $Target,
        "--remote-dir", $RemoteDir,
        "--out-dir", $OutDir,
        "--seconds", "7200",
        "--interval", "0.6",
        "--preview-width", "960",
        "--ir-gain", "$IrGain",
        "--ir-red-min", "$IrRedMin",
        "--ir-red-dominance", "$IrRedDominance",
        "--ir-reflection-max", "$IrReflectionMax",
        "--ir-local-contrast", "$IrLocalContrast"
    )
    if (-not [string]::IsNullOrWhiteSpace($IrReference)) {
        $ResolvedIrReference = (Resolve-Path $IrReference).Path
        $PreviewArgs += @("--ir-reference", $ResolvedIrReference)
    }
    Start-Process -FilePath $Python `
        -ArgumentList $PreviewArgs `
        -WorkingDirectory $ProjectDir `
        -WindowStyle Hidden `
        -RedirectStandardOutput $PreviewStdout `
        -RedirectStandardError $PreviewStderr | Out-Null

    if (-not (Get-NetTCPConnection -LocalPort $PreviewPort -State Listen -ErrorAction SilentlyContinue)) {
        Start-Process -FilePath $Python `
            -ArgumentList @("-m", "http.server", "$PreviewPort", "--bind", "127.0.0.1", "--directory", $OutDir) `
            -WindowStyle Hidden `
            -RedirectStandardOutput (Join-Path $ProjectDir "runtime\http_stdout.log") `
            -RedirectStandardError (Join-Path $ProjectDir "runtime\http_stderr.log") | Out-Null
    }

    $PreviewReady = $false
    for ($Attempt = 0; $Attempt -lt 120; $Attempt++) {
        Start-Sleep -Milliseconds 250
        if (Get-ChildItem -Path $OutDir -Filter "live_frame_*.jpg" -ErrorAction SilentlyContinue | Select-Object -First 1) {
            $PreviewReady = $true
            break
        }
    }
    if (-not $PreviewReady) {
        $PreviewError = if (Test-Path $PreviewStderr) { Get-Content $PreviewStderr -Raw } else { "no diagnostic" }
        throw "Live preview did not publish its first frame: $PreviewError"
    }

    $PreviewUrl = "http://127.0.0.1:$PreviewPort/live_preview.html"
    Start-Sleep -Seconds 2
    if (-not $NoOpenBrowser) { Start-Process $PreviewUrl }
    if ($EnableChassis) {
        Write-Host "ByteTrack chassis tracking is armed automatically." -ForegroundColor Yellow
    } else {
        Write-Host "Safe preview is running; chassis and gimbal remain disarmed." -ForegroundColor Green
    }
    Write-Host "Board path: $RemoteDir"
    Write-Host "Preview: $PreviewUrl" -ForegroundColor Green
} catch {
    if ($BoardStarted -and $EnableChassis) {
        & $Ssh @SshArgs $Target "'$RemoteDir/stop_all.sh'" 2>$null
    }
    throw
} finally {
    Remove-Item -LiteralPath $StageDir -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $Archive -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $AskPass -Force -ErrorAction SilentlyContinue
}
