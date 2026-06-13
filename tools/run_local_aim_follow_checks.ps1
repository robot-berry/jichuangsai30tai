param(
    [switch]$KeepBuild
)

$ErrorActionPreference = "Stop"

$ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ModuleDir = Join-Path $ProjectDir "aim_follow_control"
$BuildDir = Join-Path $ModuleDir ("build_local_check_" + $PID)
$TestExe = Join-Path $BuildDir "Release\aim_follow_controller_test.exe"
$FakeLogDir = Join-Path $env:TEMP "aim_follow_fake_smoke"

function Run-Step {
    param(
        [string]$Title,
        [scriptblock]$Body
    )
    Write-Host ""
    Write-Host "==== $Title ====" -ForegroundColor Cyan
    & $Body
}

function Cleanup-Temp {
    Remove-Item -LiteralPath $FakeLogDir -Recurse -Force -ErrorAction SilentlyContinue
    if (-not $KeepBuild) {
        Remove-Item -LiteralPath $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

trap {
    Cleanup-Temp
    throw
}

Write-Host "ProjectDir: $ProjectDir"

Run-Step "Prepare clean build directory" {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
}

Run-Step "Configure aim_follow_control" {
    cmake -S $ModuleDir -B $BuildDir -G "Visual Studio 17 2022" -A x64
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed."
    }
}

Run-Step "Build aim_follow_control" {
    cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed."
    }
}

Run-Step "Run aim_follow_controller_test" {
    & $TestExe
    if ($LASTEXITCODE -ne 0) {
        throw "aim_follow_controller_test failed."
    }
}

Run-Step "Smoke log analyzer self-check" {
    Remove-Item -LiteralPath $FakeLogDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $FakeLogDir | Out-Null
    Set-Content -LiteralPath (Join-Path $FakeLogDir "app.log") -Value @(
        "[AIM FOLLOW CONFIG] target_distance_m=1",
        "[AIM FOLLOW] cx=1 cy=2",
        "[DISTANCE DEBUG] bicycle"
    )
    Set-Content -LiteralPath (Join-Path $FakeLogDir "candump.log") -Value @(
        "can0  201   [8]  00 01 00 01 00 00 00 00",
        "can0  38A   [8]  AA 96 96 00 00 00 00 55"
    )
    Set-Content -LiteralPath (Join-Path $FakeLogDir "summary.txt") -Value "fake summary"
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectDir "tools\analyze_smoke_logs.ps1") -LogDir $FakeLogDir
    if ($LASTEXITCODE -ne 0) {
        throw "analyze_smoke_logs.ps1 self-check failed."
    }
}

Run-Step "Cleanup" {
    Cleanup-Temp
}

Write-Host ""
Write-Host "Local aim/follow checks passed." -ForegroundColor Green
