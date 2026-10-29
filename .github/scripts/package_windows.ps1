<#
.SYNOPSIS
    Assemble the Windows packages from a finished build tree.

.DESCRIPTION
    Writes two zips to OutDir:
      Canopy-<version>.zip      the loader, dinput8.dll, for the game folder
      Canopy-SDK-<version>.zip  the header, the docs, the example and the tools
    Canopy Studio's installer downloads the first by exactly this name, so it must not change.
    Nothing is written until check_windows_deps.py confirms every binary of ours links the MSVC
    runtime statically. Adapted from G-Diffuser's tools/package_windows.ps1.

.EXAMPLE
    pwsh .github/scripts/package_windows.ps1 -BuildDir build -Version 1.2.0
#>

param(
    [Parameter(Mandatory = $true)][string] $BuildDir,
    [Parameter(Mandatory = $true)][string] $Version,
    [string] $OutDir = "dist"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

$binaries = @(
    "src/Canopy/dinput8.dll",
    "src/CanopyTools/canopy.exe",
    "src/CanopyTools/canopy_core.dll"
) | ForEach-Object { Join-Path $BuildDir $_ }
foreach ($binary in $binaries) {
    if (-not (Test-Path $binary)) {
        Write-Error "build tree is missing $binary`nbuild first: cmake --build --preset release"
    }
}
python (Join-Path $repoRoot ".github/scripts/check_windows_deps.py") @binaries
if ($LASTEXITCODE -ne 0) {
    Write-Error "packaging refused: a binary needs a DLL the packages do not ship"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stageRoot = Join-Path $OutDir "stage-windows"
if (Test-Path $stageRoot) { Remove-Item -Recurse -Force $stageRoot }

$zips = @()
foreach ($package in @(
        @{ Component = "canopy"; Zip = "Canopy-$Version.zip" },
        @{ Component = "sdk"; Zip = "Canopy-SDK-$Version.zip" })) {
    $stage = Join-Path $stageRoot $package.Component
    cmake --install $BuildDir --component $package.Component --prefix $stage
    if ($LASTEXITCODE -ne 0) {
        Write-Error "cmake --install of $($package.Component) failed"
    }
    $zip = Join-Path $OutDir $package.Zip
    if (Test-Path $zip) { Remove-Item -Force $zip }
    Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
    $zips += $zip
}

Remove-Item -Recurse -Force $stageRoot

Write-Host ""
# Computed through .NET rather than Get-FileHash, which is missing from some constrained hosts.
foreach ($zip in $zips) {
    $stream = [System.IO.File]::OpenRead((Resolve-Path $zip))
    try {
        $digest = [System.Security.Cryptography.SHA256]::Create().ComputeHash($stream)
        "{0}  {1}" -f ([BitConverter]::ToString($digest) -replace '-', '').ToLower(), (Split-Path -Leaf $zip)
    } finally {
        $stream.Dispose()
    }
}
