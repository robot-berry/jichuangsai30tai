param(
    [string]$BoardIp = "192.168.125.171",
    [string]$BoardUser = "root",
    [string]$BoardPassword = "",
    [string]$RemoteDir = "/home/fmsh/plin_pHdmi/examples/codex/plin_main_current",
    [int]$PreviewPort = 8765
)

$ErrorActionPreference = "Stop"
function Find-SshTool {
    $Command = Get-Command "ssh.exe" -ErrorAction SilentlyContinue
    if ($Command) { return $Command.Source }
    foreach ($Candidate in @(
        (Join-Path $env:SystemRoot "System32\OpenSSH\ssh.exe"),
        (Join-Path $env:USERPROFILE ".cache\codex-runtimes\codex-primary-runtime\dependencies\native\git\usr\bin\ssh.exe")
    )) {
        if (Test-Path $Candidate) { return $Candidate }
    }
    throw "Missing ssh. Install Windows OpenSSH or Git for Windows."
}
$Ssh = Find-SshTool
if ([string]::IsNullOrWhiteSpace($BoardPassword)) {
    $BoardPassword = if ($env:BOARD_PASS) { $env:BOARD_PASS } else { Read-Host "Board SSH password" }
}

$AskPass = Join-Path $env:TEMP ("plin_stop_askpass_" + [Guid]::NewGuid().ToString("N") + ".cmd")
try {
    [IO.File]::WriteAllText($AskPass, "@echo off`r`necho %BOARD_PASS%`r`n", [Text.Encoding]::ASCII)
    $env:BOARD_PASS = $BoardPassword
    $env:SSH_ASKPASS = $AskPass
    $env:SSH_ASKPASS_REQUIRE = "force"
    $env:DISPLAY = "codex"
    & $Ssh -o StrictHostKeyChecking=no "$BoardUser@$BoardIp" "if [ -x '$RemoteDir/stop_all.sh' ]; then '$RemoteDir/stop_all.sh'; fi"

    Get-CimInstance Win32_Process | Where-Object {
        ($_.Name -eq "python.exe" -and $_.CommandLine -match "preview_plin_network_frames.py|http.server.+$PreviewPort") -or
        ($_.Name -eq "ssh.exe" -and $_.CommandLine -match "stream_plin_hdmi_udma.py")
    } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }

    Write-Host "Tracking, vision, CAN, and preview are stopped." -ForegroundColor Green
} finally {
    Remove-Item -LiteralPath $AskPass -Force -ErrorAction SilentlyContinue
}
