param(
    [string]$BoardIp = "192.168.125.171",
    [string]$User = "root",
    [int]$SshPort = 22,
    [int]$TcpTimeoutMs = 3000
)

$ErrorActionPreference = "Continue"
$SshTarget = "$User@$BoardIp"

Write-Host "==== 30TAI connection preflight ====" -ForegroundColor Cyan
Write-Host "Board: $SshTarget"
Write-Host "Port:  $SshPort"
Write-Host ""

function Test-TcpPortFast {
    param(
        [string]$HostName,
        [int]$Port,
        [int]$TimeoutMs
    )

    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $task = $client.ConnectAsync($HostName, $Port)
        if (-not $task.Wait($TimeoutMs)) {
            return $false
        }
        return $client.Connected
    } catch {
        return $false
    } finally {
        $client.Close()
    }
}

Write-Host "[1/6] Local IPv4 addresses" -ForegroundColor Cyan
Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object { $_.IPAddress -notlike "169.254.*" -and $_.IPAddress -ne "127.0.0.1" } |
    Select-Object InterfaceAlias, IPAddress, PrefixLength |
    Format-Table -AutoSize

Write-Host "[2/6] Route candidates for board IP" -ForegroundColor Cyan
Get-NetRoute -AddressFamily IPv4 |
    Where-Object { $_.DestinationPrefix -ne "255.255.255.255/32" } |
    Sort-Object RouteMetric, InterfaceMetric |
    Select-Object -First 12 DestinationPrefix, NextHop, InterfaceAlias, RouteMetric, InterfaceMetric |
    Format-Table -AutoSize

Write-Host "[3/6] ARP entry before ping" -ForegroundColor Cyan
arp -a | Select-String $BoardIp

Write-Host "[4/6] Ping test" -ForegroundColor Cyan
ping -n 2 -w 1000 $BoardIp

Write-Host "[5/6] TCP port test" -ForegroundColor Cyan
$tcpOk = Test-TcpPortFast -HostName $BoardIp -Port $SshPort -TimeoutMs $TcpTimeoutMs
Write-Host "TCP $BoardIp`:$SshPort reachable: $tcpOk"

Write-Host "[6/6] SSH batch test" -ForegroundColor Cyan
ssh -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=no -p $SshPort $SshTarget "echo connected"
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "SSH did not complete. If ping/TCP also fail, check board power, cable, IP address, and PC network segment." -ForegroundColor Yellow
    Write-Host "If TCP succeeds but SSH batch fails, the board is reachable; run deploy_30tai.ps1 and enter the password when prompted." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "[ARP entry after tests]" -ForegroundColor Cyan
arp -a | Select-String $BoardIp
