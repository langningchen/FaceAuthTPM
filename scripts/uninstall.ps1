# SPDX-License-Identifier: GPL-3.0-only
param([switch]$PurgeData)
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'disable-provider.ps1')
$service=Get-Service -Name 'FaceAuthModel' -ErrorAction SilentlyContinue
if($service){
    if($service.Status -ne 'Stopped'){Stop-Service -Name 'FaceAuthModel' -Force -ErrorAction SilentlyContinue}
    & sc.exe delete FaceAuthModel | Out-Null
}
$install='C:\Program Files\FaceAuth';$data='C:\ProgramData\FaceAuth'
if(Test-Path $install){Remove-Item -Recurse -Force $install}
if($PurgeData -and (Test-Path $data)){Remove-Item -Recurse -Force $data}
Write-Host 'FaceAuth binaries removed. Use -PurgeData to also delete TPM-encrypted credentials and face templates.'
