param(
    [string]$Subnet = "192.168.125",
    [int]$Port = 22,
    [int]$Start = 1,
    [int]$End = 254,
    [int]$TimeoutMs = 250
)

$ErrorActionPreference = "Continue"

Write-Host "==== 30TAI board candidate scan ====" -ForegroundColor Cyan
Write-Host "Subnet: ${Subnet}.x"
Write-Host "Port:   $Port"
Write-Host "Range:  $Start-$End"
Write-Host ""

$candidates = New-Object System.Collections.Generic.List[object]

for ($i = $Start; $i -le $End; ++$i) {
    $ip = "$Subnet.$i"
    $client = New-Object System.Net.Sockets.TcpClient
    $iar = $client.BeginConnect($ip, $Port, $null, $null)
    $ok = $iar.AsyncWaitHandle.WaitOne($TimeoutMs, $false)
    if ($ok -and $client.Connected) {
        try {
            $client.EndConnect($iar)
        } catch {
        }
        $candidates.Add([pscustomobject]@{
            IP = $ip
            Port = $Port
            Status = "open"
        })
        Write-Host "OPEN  $ip`:$Port" -ForegroundColor Green
    }
    $client.Close()
}

Write-Host ""
Write-Host "ARP entries on subnet:" -ForegroundColor Cyan
arp -a | Select-String "$Subnet."

Write-Host ""
if ($candidates.Count -eq 0) {
    Write-Host "No host with TCP port $Port open was found in ${Subnet}.$Start-${End}." -ForegroundColor Yellow
    Write-Host "Check board power, Ethernet link, static IP, and firewall/SSH service."
    exit 1
}

Write-Host "Candidates:" -ForegroundColor Cyan
$candidates | Format-Table -AutoSize
exit 0
