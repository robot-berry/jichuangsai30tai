param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir
)

$ErrorActionPreference = "Stop"

$ProjectDir = (Resolve-Path $ProjectDir).Path
$CMakePath = Join-Path $ProjectDir "CMakeLists.txt"
$MainPath = Join-Path $ProjectDir "src\sdicamera+yolov5+hdmi.cpp"
$BuildScript = Join-Path $ProjectDir "build_30tai.sh"
$ModuleHeader = Join-Path $ProjectDir "aim_follow_control\include\aim_follow_controller.hpp"
$ModuleSource = Join-Path $ProjectDir "aim_follow_control\src\aim_follow_controller.cpp"
$SmokeScript = Join-Path $ProjectDir "aim_follow_control\test\run_30tai_smoke_test.sh"

$checks = New-Object System.Collections.Generic.List[object]

function Add-Check {
    param(
        [string]$Name,
        [bool]$Pass,
        [string]$Evidence
    )
    $checks.Add([pscustomobject]@{
        Name = $Name
        Pass = $Pass
        Evidence = $Evidence
    }) | Out-Null
}

function Read-TextIfExists {
    param([string]$Path)
    if (Test-Path $Path) {
        return Get-Content -LiteralPath $Path -Raw
    }
    return ""
}

$cmakeText = Read-TextIfExists $CMakePath
$mainText = Read-TextIfExists $MainPath
$moduleHeaderText = Read-TextIfExists $ModuleHeader
$moduleSourceText = Read-TextIfExists $ModuleSource

Add-Check "Project has build_30tai.sh" (Test-Path $BuildScript) $BuildScript
Add-Check "Module header exists" (Test-Path $ModuleHeader) $ModuleHeader
Add-Check "Module source exists" (Test-Path $ModuleSource) $ModuleSource
Add-Check "Module exposes MonocularDistanceEstimator" ($moduleHeaderText.Contains("MonocularDistanceEstimator")) $ModuleHeader
Add-Check "Module implements monocular distance update" ($moduleSourceText.Contains("target_real_width_m") -and $moduleSourceText.Contains("filtered_distance_m_")) $ModuleSource
Add-Check "Board smoke script exists" (Test-Path $SmokeScript) $SmokeScript
Add-Check "CMake includes aim_follow source" ($cmakeText.Contains("aim_follow_control/src/aim_follow_controller.cpp")) $CMakePath
Add-Check "CMake includes aim_follow include dir" ($cmakeText.Contains("aim_follow_control/include")) $CMakePath
Add-Check "Main includes aim_follow header" ($mainText.Contains('#include "aim_follow_controller.hpp"')) $MainPath
Add-Check "Main defines AIM_FOLLOW enable switch" ($mainText.Contains("AIM_FOLLOW_CONTROL_ENABLE")) $MainPath
Add-Check "Main defines target distance parameter" ($mainText.Contains("AIM_FOLLOW_TARGET_DISTANCE_M")) $MainPath
Add-Check "Main creates TargetSelector" ($mainText.Contains("aim_follow::TargetSelector")) $MainPath
Add-Check "Main creates AimFollowController" ($mainText.Contains("aim_follow::AimFollowController")) $MainPath
Add-Check "Main uses MonocularDistanceEstimator" ($mainText.Contains("aim_follow::MonocularDistanceEstimator")) $MainPath
Add-Check "Main maps detection boxes before target selection" ($mainText.Contains("target_candidates") -and $mainText.Contains("map_box_to_display")) $MainPath
Add-Check "Main logs aim/follow state" ($mainText.Contains("[AIM FOLLOW]")) $MainPath
Add-Check "Main logs distance debug state" ($mainText.Contains("[DISTANCE DEBUG]")) $MainPath
Add-Check "Main references chassis CAN 0x201" ($mainText.Contains("0x201") -or $mainText.Contains("CHASSIS_CAN_ID")) $MainPath
Add-Check "Main references gimbal CAN 0x38A" ($mainText.Contains("0x38A") -or $mainText.Contains("GIMBAL_CAN_ID")) $MainPath
Add-Check "Main sends chassis command" ($mainText.Contains("send_chassis_can_mode")) $MainPath
Add-Check "Main sends gimbal command" ($mainText.Contains("send_gimbal_can_mode")) $MainPath

$failed = @($checks | Where-Object { -not $_.Pass })

Write-Host "==== PLin aim/follow integration check ===="
Write-Host "ProjectDir: $ProjectDir"
Write-Host ""

foreach ($check in $checks) {
    if ($check.Pass) {
        Write-Host ("[PASS] " + $check.Name) -ForegroundColor Green
    } else {
        Write-Host ("[FAIL] " + $check.Name) -ForegroundColor Red
    }
    Write-Host ("       " + $check.Evidence)
}

Write-Host ""
if ($failed.Count -gt 0) {
    Write-Host "Integration check failed: $($failed.Count) item(s) need attention." -ForegroundColor Red
    exit 1
}

Write-Host "Integration check passed." -ForegroundColor Green
