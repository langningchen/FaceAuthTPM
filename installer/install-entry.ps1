#Requires -RunAsAdministrator
# SPDX-License-Identifier: GPL-3.0-only
# Inno Setup entry point. Runs the hardened first-install/update scripts from
# the temporary deployment tree without automatically enabling FaceAuth on a
# first installation.

param([Parameter(Mandatory=$true)][string]$PayloadRoot)
$ErrorActionPreference='Stop'

$installedDll='C:\Program Files\FaceAuth\FaceAuthCredentialProvider.dll'
$providerGuid='{4CF34D82-0D5D-4A5C-9E46-64C16F348C62}'
$providerKey="Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$providerGuid"
$scripts=Join-Path $PayloadRoot 'scripts'
$build=Join-Path $PayloadRoot 'out\Release'

if(-not(Test-Path (Join-Path $scripts 'install.ps1'))){throw "Invalid installer payload: $PayloadRoot"}

if(-not(Test-Path $installedDll)){
    Write-Host 'Performing first-time FaceAuthTPM installation...' -ForegroundColor Cyan
    & (Join-Path $scripts 'install.ps1') -BuildDir $build
    if($LASTEXITCODE){exit $LASTEXITCODE}
    Write-Host 'FaceAuthTPM installed. Credential Provider remains disabled until configuration is validated.' -ForegroundColor Green
    exit 0
}

$wasEnabled=Test-Path -LiteralPath $providerKey
Write-Host "Existing FaceAuthTPM installation detected. Provider enabled before update: $wasEnabled" -ForegroundColor Cyan
& (Join-Path $scripts 'update-installed.ps1') -BuildDir $build
if($LASTEXITCODE){exit $LASTEXITCODE}

# update-installed.ps1 re-enables a provider that was updated successfully.
# Preserve the user's previous disabled state across an installer upgrade.
if(-not $wasEnabled -and (Test-Path -LiteralPath $providerKey)){
    & (Join-Path $scripts 'disable-provider.ps1')
    if($LASTEXITCODE){exit $LASTEXITCODE}
    Write-Host 'Preserved the previously disabled Credential Provider state.' -ForegroundColor Green
}
