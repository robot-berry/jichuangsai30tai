param(
    [string]$PlinProjectDir = "",
    [string]$PackageRoot = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$archivePath = Join-Path $repoRoot "sdk\fpai_demo_package_26010502_deps.zip"
$partsDir = Join-Path $repoRoot "sdk\fpai_demo_package_26010502_deps_parts"

if (!(Test-Path -LiteralPath $archivePath)) {
    $parts = @()
    if (Test-Path -LiteralPath $partsDir) {
        $parts = Get-ChildItem -LiteralPath $partsDir -File -Filter "fpai_demo_package_26010502_deps.zip.part*" |
            Sort-Object Name
    }

    if ($parts.Count -eq 0) {
        throw "SDK dependency archive or split parts not found. Expected $archivePath or $partsDir"
    }

    Write-Host "Reconstructing SDK dependency archive from split parts..."
    $buffer = New-Object byte[] (4MB)
    $output = [System.IO.File]::Create($archivePath)
    try {
        foreach ($part in $parts) {
            Write-Host "  adding $($part.Name)"
            $input = [System.IO.File]::OpenRead($part.FullName)
            try {
                while (($read = $input.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $output.Write($buffer, 0, $read)
                }
            } finally {
                $input.Dispose()
            }
        }
    } finally {
        $output.Dispose()
    }
}

if ([string]::IsNullOrWhiteSpace($PlinProjectDir)) {
    $PlinProjectDir = Join-Path $repoRoot "examples\plin_yolov5_hdmi_integrated"
}

$plinResolved = Resolve-Path -LiteralPath $PlinProjectDir

if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
    # The PLin CMakeLists.txt checks ../../../fpai_demo_package_26010502/deps.
    $expectedParent = Resolve-Path (Join-Path $plinResolved.Path "..\..\..")
    $PackageRoot = Join-Path $expectedParent.Path "fpai_demo_package_26010502"
}

$depsDir = Join-Path $PackageRoot "deps"

if ((Test-Path -LiteralPath $depsDir) -and !$Force) {
    Write-Host "SDK deps already exist: $depsDir"
    Write-Host "Use -Force to overwrite by extracting the bundled archive again."
} else {
    New-Item -ItemType Directory -Force -Path $PackageRoot | Out-Null
    Expand-Archive -LiteralPath $archivePath -DestinationPath $PackageRoot -Force:$Force
}

$requiredPaths = @(
    "deps\modelzoo_utils\include\ai_example\postprocesses.hpp",
    "deps\modelzoo_utils\include\ai_example\yolov5_npu_actor.hpp",
    "deps\modelzoo_utils\include\pipeline\actor\base_actors.hpp",
    "deps\thirdparty\include",
    "deps\thirdparty\a",
    "deps\thirdparty\lib"
)

$missing = @()
foreach ($rel in $requiredPaths) {
    $path = Join-Path $PackageRoot $rel
    if (!(Test-Path -LiteralPath $path)) {
        $missing += $path
    }
}

if ($missing.Count -gt 0) {
    Write-Host "Missing expected SDK dependency paths:" -ForegroundColor Red
    foreach ($path in $missing) {
        Write-Host "  $path" -ForegroundColor Red
    }
    exit 1
}

Write-Host "SDK deps are ready:" -ForegroundColor Green
Write-Host "  $depsDir"
Write-Host ""
Write-Host "This path matches the PLin CMake fallback:"
Write-Host "  ../../../fpai_demo_package_26010502/deps"
