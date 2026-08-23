# SPDX-License-Identifier: GPL-3.0-only
[CmdletBinding()]
param(
    [string]$Version,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Version) { $Version = (Get-Content (Join-Path $root 'VERSION.txt') -Raw).Trim() }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Invalid version: $Version (expected x.y.z)" }

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

& (Join-Path $PSScriptRoot 'make-deploy.ps1')
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# The model license files are committed under third_party; fetch-models.ps1 only downloads the ONNX binaries.

$thirdParty = Join-Path $root 'third_party'
New-Item -ItemType Directory -Force $thirdParty | Out-Null

# Copy copyright/license files for every vcpkg package that may contribute
# redistributed runtime DLLs. This is broader than only OpenCV and keeps the
# installer compliant when the vcpkg dependency graph changes.
$vcpkgLicenseDir = Join-Path $thirdParty 'vcpkg-licenses'
if (Test-Path $vcpkgLicenseDir) { Remove-Item -Recurse -Force $vcpkgLicenseDir }
New-Item -ItemType Directory -Force $vcpkgLicenseDir | Out-Null
$vcpkgCopyrights = @(Get-ChildItem -Path (Join-Path $root 'out\vcpkg_installed\x64-windows\share') -File -Filter 'copyright' -Recurse -ErrorAction SilentlyContinue)
if ($vcpkgCopyrights.Count -eq 0) { throw 'Could not locate vcpkg copyright files under out\vcpkg_installed\x64-windows\share.' }
foreach ($copyright in $vcpkgCopyrights) {
    $packageName = Split-Path -Leaf (Split-Path -Parent $copyright.FullName)
    Copy-Item -Force $copyright.FullName (Join-Path $vcpkgLicenseDir ($packageName + '-LICENSE.txt'))
}
Write-Host "Collected $($vcpkgCopyrights.Count) vcpkg package license files." -ForegroundColor Cyan

# Bundle the x64 Visual C++ redistributable when it is available in Visual Studio.
$payloadDir = Join-Path $root 'installer\payload'
if (Test-Path $payloadDir) { Remove-Item -Recurse -Force $payloadDir }
New-Item -ItemType Directory -Force $payloadDir | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vsInstall = (& $vswhere -latest -products '*' -property installationPath | Select-Object -First 1)
    if ($vsInstall) {
        $redist = Get-ChildItem -Path (Join-Path $vsInstall 'VC\Redist\MSVC') -Filter 'vc_redist.x64.exe' -File -Recurse -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($redist) {
            Copy-Item -Force $redist.FullName (Join-Path $payloadDir 'vc_redist.x64.exe')
            Write-Host "Bundled VC++ redistributable: $($redist.FullName)" -ForegroundColor Cyan
        } else {
            Write-Warning 'VC++ redistributable was not found; Setup will be built without bundling it.'
        }
    }
}

$dist = Join-Path $root 'dist'
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item -Force (Join-Path $root 'release\FaceAuthTPM-Configure.ps1') (Join-Path $dist 'FaceAuthTPM-Configure.ps1')

$iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1)
if (-not $iscc) {
    foreach ($candidate in @(
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
    )) {
        if ($candidate -and (Test-Path $candidate)) { $iscc = $candidate; break }
    }
}
if (-not $iscc) { throw 'Inno Setup 6 compiler (ISCC.exe) was not found.' }

$iss = Join-Path $root 'installer\FaceAuthTPM.iss'
& $iscc "/DMyAppVersion=$Version" $iss
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$setup = Join-Path $dist 'FaceAuthTPM-Setup.exe'
$config = Join-Path $dist 'FaceAuthTPM-Configure.ps1'
if (-not (Test-Path $setup)) { throw "Installer was not created: $setup" }
if (-not (Test-Path $config)) { throw "CLI configuration file was not created: $config" }

$extra = @(Get-ChildItem -LiteralPath $dist -File | Where-Object { $_.Name -notin @('FaceAuthTPM-Setup.exe','FaceAuthTPM-Configure.ps1') })
if ($extra.Count -gt 0) { throw "Release dist contains unexpected files: $($extra.Name -join ', ')" }

Write-Host 'Release package complete:' -ForegroundColor Green
Get-ChildItem -LiteralPath $dist -File | ForEach-Object {
    $hash = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash
    Write-Host ("  {0}  SHA256={1}" -f $_.Name, $hash)
}
