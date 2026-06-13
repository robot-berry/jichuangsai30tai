param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir
)

$ErrorActionPreference = "Stop"

$DeployScript = Join-Path $PSScriptRoot "deploy_30tai.ps1"

powershell -NoProfile -ExecutionPolicy Bypass -File $DeployScript -ProjectDir $ProjectDir -DryRun -Build -SmokeTest -FetchLogs
if ($LASTEXITCODE -ne 0) {
    throw "deploy_30tai.ps1 dry run failed."
}

Write-Host "Deploy dry run passed." -ForegroundColor Green
