<#
.SYNOPSIS
    Assemble the Windows packages from a finished build tree and a published Canopy Studio.

.DESCRIPTION
    Writes four zips to OutDir:
      Canopy-<version>.zip                    the loader, dinput8.dll, for the game folder
      VillageQoL-<version>.zip                the VillageQoL mod, mods\VillageQoL.dll
      Canopy-SDK-<version>.zip                the header, the docs, the example and the tools
      CanopyStudio-<version>-windows-x64.zip  Canopy Studio, self-contained
    Canopy Studio's installer downloads the first two by exactly these names, so they must not
    change. Nothing is written until check_windows_deps.py confirms every binary of ours links
    the MSVC runtime statically, and the Studio folder is checked by name for what it cannot
    start without. Adapted from G-Diffuser's tools/package_windows.ps1.

.EXAMPLE
    pwsh .github/scripts/package_windows.ps1 -BuildDir build -StudioDir build/studio-win -Version 1.2.0
#>

param(
    [Parameter(Mandatory = $true)][string] $BuildDir,
    [Parameter(Mandatory = $true)][string] $StudioDir,
    [Parameter(Mandatory = $true)][string] $Version,
    [string] $OutDir = "dist"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

$binaries = @(
    "src/Canopy/dinput8.dll",
    "src/VillageQoL/VillageQoL.dll",
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

# Canopy Studio cannot start without these; the rest of the folder is the .NET runtime and
# Avalonia's native libraries, which the publish step owns.
foreach ($item in @("CanopyStudio.exe", "CanopyStudio.dll", "canopy_core.dll")) {
    if (-not (Test-Path (Join-Path $StudioDir $item))) {
        Write-Error "the published Canopy Studio is missing $item"
    }
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stageRoot = Join-Path $OutDir "stage-windows"
if (Test-Path $stageRoot) { Remove-Item -Recurse -Force $stageRoot }

$zips = @()
foreach ($package in @(
        @{ Component = "canopy"; Zip = "Canopy-$Version.zip" },
        @{ Component = "villageqol"; Zip = "VillageQoL-$Version.zip" },
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

# Canopy Studio's zip has one top folder, so extracting it never scatters files.
$studioStage = Join-Path $stageRoot "studio/CanopyStudio"
New-Item -ItemType Directory -Force -Path $studioStage | Out-Null
Copy-Item -Recurse -Force (Join-Path $StudioDir "*") $studioStage
Copy-Item -Force (Join-Path $repoRoot "LICENSE") $studioStage
Copy-Item -Force (Join-Path $repoRoot "THIRD_PARTY_NOTICES.txt") $studioStage
$studioZip = Join-Path $OutDir "CanopyStudio-$Version-windows-x64.zip"
if (Test-Path $studioZip) { Remove-Item -Force $studioZip }
Compress-Archive -Path $studioStage -DestinationPath $studioZip
$zips += $studioZip

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
