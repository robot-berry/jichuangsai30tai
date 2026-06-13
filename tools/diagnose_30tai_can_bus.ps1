param(
    [string]$BoardIp = "192.168.125.171",
    [string]$User = "root",
    [string]$SshKey = "",
    [string]$CanIface = "can0",
    [int]$Bitrate = 250000,
    [int]$ObserveSeconds = 5,
    [string]$LocalLogDir = "",
    [switch]$ConfigureCan,
    [switch]$SendNeutralFrames,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$SshTarget = "$User@$BoardIp"
$SshOptionArgs = @("-o", "ConnectTimeout=10", "-o", "StrictHostKeyChecking=no")
if (-not [string]::IsNullOrWhiteSpace($SshKey)) {
    $SshKey = (Resolve-Path $SshKey).Path
    $SshOptionArgs += @("-i", $SshKey)
}

if ([string]::IsNullOrWhiteSpace($LocalLogDir)) {
    $LocalLogDir = Join-Path $RepoRoot "board_can_diagnostics"
}

$RemoteLogDir = "/tmp/aim_follow_can_diag_$(Get-Date -Format 'yyyyMMdd_HHmmss')"

Write-Host "==== 30TAI CAN bus diagnostic ====" -ForegroundColor Cyan
Write-Host "Board:        $SshTarget"
Write-Host "CAN iface:    $CanIface"
Write-Host "Bitrate:      $Bitrate"
Write-Host "Observe:      ${ObserveSeconds}s"
Write-Host "ConfigureCan: $ConfigureCan"
Write-Host "NeutralSend:  $SendNeutralFrames"
Write-Host "RemoteLogDir: $RemoteLogDir"
Write-Host "LocalLogDir:  $LocalLogDir"
if (-not [string]::IsNullOrWhiteSpace($SshKey)) {
    Write-Host "SSH key:      $SshKey"
}

$remoteScript = @"
set +e

LOG_DIR='$RemoteLogDir'
CAN_IFACE='$CanIface'
BITRATE='$Bitrate'
OBSERVE_SECONDS='$ObserveSeconds'
CONFIGURE_CAN='$($ConfigureCan.IsPresent)'
SEND_NEUTRAL='$($SendNeutralFrames.IsPresent)'

mkdir -p "`$LOG_DIR"

capture_status() {
    label="`$1"
    {
        echo "[`$label]"
        date
        echo
        ip -details -statistics link show "`$CAN_IFACE" 2>&1
        echo
        echo "[proc net can stats]"
        cat /proc/net/can/stats 2>/dev/null || true
        echo
        echo "[receive filters]"
        cat /proc/net/can/rcvlist_all 2>/dev/null || true
    } > "`$LOG_DIR/`$label.txt" 2>&1
}

capture_status before

if [ "`$CONFIGURE_CAN" = "True" ]; then
    {
        ip link set "`$CAN_IFACE" down 2>/dev/null || true
        ip link set "`$CAN_IFACE" type can bitrate "`$BITRATE" restart-ms 100
        ip link set "`$CAN_IFACE" up
        echo "[configure_ret] `$?"
    } > "`$LOG_DIR/configure.txt" 2>&1
    capture_status after_configure
fi

if command -v candump >/dev/null 2>&1; then
    timeout "`$OBSERVE_SECONDS" candump -L "`$CAN_IFACE" > "`$LOG_DIR/candump_observe.log" 2>&1 &
    dump_pid=`$!
else
    echo "candump not found" > "`$LOG_DIR/candump_observe.log"
    dump_pid=
fi

if [ "`$SEND_NEUTRAL" = "True" ]; then
    {
        if command -v cansend >/dev/null 2>&1; then
            # Neutral chassis: motor1=0, motor2=0, pitch=150, yaw=150, trigger=0, enable=1.
            cansend "`$CAN_IFACE" 201#0000000096960001
            echo "cansend 201 ret=`$?"
            # Neutral gimbal: AA pitch yaw trigger 00 00 00 55.
            cansend "`$CAN_IFACE" 38A#AA96960000000055
            echo "cansend 38A ret=`$?"
        else
            echo "cansend not found"
        fi
    } > "`$LOG_DIR/neutral_send.log" 2>&1
fi

if [ -n "`$dump_pid" ]; then
    wait "`$dump_pid" 2>/dev/null || true
fi

capture_status after

python3 - "`$LOG_DIR" <<'PY' > "`$LOG_DIR/summary.md"
import re
import sys
from pathlib import Path

log_dir = Path(sys.argv[1])
text = (log_dir / "after.txt").read_text(errors="ignore") if (log_dir / "after.txt").exists() else ""
before = (log_dir / "before.txt").read_text(errors="ignore") if (log_dir / "before.txt").exists() else ""

def find(pattern, source, default="unknown"):
    m = re.search(pattern, source)
    return m.group(1) if m else default

state = find(r"can state ([A-Z-]+)", text)
tx = find(r"berr-counter tx (\d+)", text, "unknown")
rx = find(r"berr-counter tx \d+ rx (\d+)", text, "unknown")
bitrate = find(r"bitrate (\d+)", text, "unknown")
tx_packets = find(r"TX:\s+bytes\s+packets.*?\n\s+\d+\s+(\d+)", text, "unknown")
rx_packets = find(r"RX:\s+bytes\s+packets.*?\n\s+\d+\s+(\d+)", text, "unknown")
before_state = find(r"can state ([A-Z-]+)", before)

print("# 30TAI CAN Bus Diagnostic Summary")
print()
print(f"- Before state: `{before_state}`")
print(f"- After state: `{state}`")
print(f"- Bitrate: `{bitrate}`")
print(f"- Error counters: tx=`{tx}`, rx=`{rx}`")
print(f"- Packets: tx=`{tx_packets}`, rx=`{rx_packets}`")
print()
if state == "ERROR-ACTIVE":
    print("Result: CAN controller is active. It is reasonable to continue with lifted-wheel neutral-frame or synthetic-control tests.")
elif state == "ERROR-PASSIVE":
    print("Result: CAN controller is ERROR-PASSIVE. Do not send motion commands yet.")
    print()
    print("Likely checks: controller power, CANH/CANL direction, common ground, 120-ohm termination, bitrate, and whether the vehicle controller has entered CAN bus control mode.")
elif state == "BUS-OFF":
    print("Result: CAN controller is BUS-OFF. Reset/configure the interface only after fixing wiring or bitrate.")
else:
    print("Result: CAN state is not ready or could not be parsed. Check raw logs.")

dump = log_dir / "candump_observe.log"
if dump.exists():
    sample = dump.read_text(errors="ignore").strip().splitlines()
    print()
    print(f"- candump sample lines: `{len(sample)}`")
PY

echo "`$LOG_DIR"
"@

if ($DryRun) {
    Write-Host ""
    Write-Host "[DryRun] Remote script would be executed on $SshTarget"
    Write-Host $remoteScript
    exit 0
}

Write-Host ""
Write-Host "[Upload and run remote CAN diagnostic]" -ForegroundColor Cyan
& ssh @SshOptionArgs $SshTarget "mkdir -p '$RemoteLogDir'"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to create remote log dir: $RemoteLogDir"
}

$localScript = Join-Path $env:TEMP "diagnose_30tai_can_bus.sh"
Set-Content -LiteralPath $localScript -Value $remoteScript -Encoding ASCII
& scp @SshOptionArgs $localScript "${SshTarget}:$RemoteLogDir/diagnose_can.sh"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to upload CAN diagnostic script."
}

& ssh @SshOptionArgs $SshTarget "bash '$RemoteLogDir/diagnose_can.sh'"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Remote CAN diagnostic returned nonzero; fetching logs anyway." -ForegroundColor Yellow
}

New-Item -ItemType Directory -Force -Path $LocalLogDir | Out-Null
$localRunDir = Join-Path $LocalLogDir (Split-Path $RemoteLogDir -Leaf)

Write-Host ""
Write-Host "[Fetch CAN diagnostic logs]" -ForegroundColor Cyan
& scp -r @SshOptionArgs "${SshTarget}:$RemoteLogDir" "$localRunDir"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to fetch CAN diagnostic logs."
}

Write-Host ""
Write-Host "Fetched CAN diagnostic logs to: $localRunDir" -ForegroundColor Green
$summaryPath = Join-Path $localRunDir "summary.md"
if (Test-Path $summaryPath) {
    Write-Host ""
    Write-Host "[CAN diagnostic summary]" -ForegroundColor Cyan
    Get-Content -Raw $summaryPath
}
