#Requires -RunAsAdministrator
# SPDX-License-Identifier: GPL-3.0-only
param([string]$BuildDir)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(-not $BuildDir){$BuildDir=Join-Path $root 'out\Release'}
$install='C:\Program Files\FaceAuth'
foreach($name in @('FaceAuthSensor.exe','FaceAuthCameraProbe.exe','FaceAuthModelService.exe')){
    if(-not(Test-Path (Join-Path $BuildDir $name))){throw "Build output missing: $name"}
}
if(Get-Process -Name FaceAuthSensor -ErrorAction SilentlyContinue){
    throw 'FaceAuthSensor.exe is currently running. Unlock the desktop normally and retry. No reboot is needed.'
}
$service=Get-Service -Name 'FaceAuthModel' -ErrorAction SilentlyContinue
if($service -and $service.Status -ne 'Stopped'){
    Write-Host 'Stopping model preloader briefly...' -ForegroundColor Cyan
    Stop-Service -Name 'FaceAuthModel' -Force
    $service.WaitForStatus('Stopped',[TimeSpan]::FromSeconds(10))
}
Copy-Item -Force (Join-Path $BuildDir 'FaceAuthSensor.exe') $install
Copy-Item -Force (Join-Path $BuildDir 'FaceAuthCameraProbe.exe') $install
Copy-Item -Force (Join-Path $BuildDir 'FaceAuthModelService.exe') $install
if($service){
    Start-Service -Name 'FaceAuthModel'
    (Get-Service -Name 'FaceAuthModel').WaitForStatus('Running',[TimeSpan]::FromSeconds(20))
}
Write-Host 'Camera/Sensor components updated. Credential Provider DLL and TPM/enrollment data were not touched.' -ForegroundColor Green
Write-Host 'No reboot is required.' -ForegroundColor Green
Write-Host 'Recommended next step:' -ForegroundColor Cyan
Write-Host '  .\scripts\test-camera-formats.ps1 -Backend msmf -CooldownMs 5000'
Write-Host 'Then save the fastest candidate with -SaveFormat and test FaceAuthSensor.exe directly.'
