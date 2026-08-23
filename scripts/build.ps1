# SPDX-License-Identifier: GPL-3.0-only
param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [switch]$AllowRemoteForVm
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (-not $VcpkgRoot) {
    foreach($candidate in @('C:\vcpkg','C:\src\vcpkg')) { if(Test-Path (Join-Path $candidate 'scripts\buildsystems\vcpkg.cmake')) { $VcpkgRoot=$candidate; break } }
}
if (-not $VcpkgRoot -or -not (Test-Path (Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'))) {
    throw 'vcpkg not found. Set VCPKG_ROOT to your vcpkg directory.'
}
& (Join-Path $PSScriptRoot 'fetch-models.ps1')
$toolchain=Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
$out=Join-Path $root ($(if($AllowRemoteForVm){'out-vmtest'}else{'out'}))
$remote=$(if($AllowRemoteForVm){'ON'}else{'OFF'})
cmake --fresh -S $root -B $out -G "Visual Studio 18 2026" -A x64 "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DVCPKG_TARGET_TRIPLET=x64-windows "-DFACEAUTH_TEST_ALLOW_REMOTE=$remote"
if($LASTEXITCODE){exit $LASTEXITCODE}
cmake --build $out --config Release --parallel
if($LASTEXITCODE){exit $LASTEXITCODE}
if($AllowRemoteForVm){
    'THIS BUILD ALLOWS FACEAUTH IN RDP/HYPER-V ENHANCED SESSION. DO NOT INSTALL IT ON THE REAL HOST.' | Set-Content (Join-Path $out 'Release\VM_TEST_BUILD_DO_NOT_INSTALL_ON_HOST.txt')
}
