# SPDX-License-Identifier: GPL-3.0-only
param(
    [int]$Camera=0,
    [ValidateSet('msmf','dshow','any')][string]$Backend='msmf',
    [int]$CooldownMs=5000,
    [int]$Fps=30,
    [string]$Exe
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(-not $Exe){
    $installed='C:\Program Files\FaceAuth\FaceAuthCameraProbe.exe'
    $built=Join-Path $root 'out\Release\FaceAuthCameraProbe.exe'
    if(Test-Path $installed){$Exe=$installed}
    elseif(Test-Path $built){$Exe=$built}
    else{throw 'FaceAuthCameraProbe.exe not found. Build FaceAuthTPM first or install the camera component update.'}
}

$tests=@(
    @{Name='native/default'; W=0; H=0},
    @{Name='2560x1440'; W=2560; H=1440},
    @{Name='1920x1080'; W=1920; H=1080},
    @{Name='1280x720'; W=1280; H=720},
    @{Name='640x480'; W=640; H=480}
)

Write-Host "FaceAuth media-type startup matrix ($Backend, cooldown ${CooldownMs}ms)" -ForegroundColor Cyan
Write-Host 'Each mode is opened once. The cooldown is intentional so the camera firmware/driver can fully release between modes.'
foreach($t in $tests){
    Write-Host "`n=== $($t.Name) ===" -ForegroundColor Yellow
    $args=@('--camera',"$Camera",'--repeat','1','--backend',$Backend,'--cooldown-ms',"$CooldownMs")
    if($t.W -gt 0){$args+=@('--width',"$($t.W)",'--height',"$($t.H)",'--fps',"$Fps")}
    & $Exe @args
    if($LASTEXITCODE -ne 0){Write-Warning "$($t.Name) failed with exit code $LASTEXITCODE"}
}
Write-Host "`nTo save a candidate for the real LogonUI Sensor, rerun for example:" -ForegroundColor Cyan
Write-Host "  .\scripts\test-camera-speed.ps1 -Backend $Backend -Width 2560 -Height 1440 -Fps $Fps -Repeat 1 -CooldownMs $CooldownMs -SaveBest -SaveFormat"
Write-Host 'To restore native/default format:'
Write-Host "  .\scripts\test-camera-speed.ps1 -Backend $Backend -Repeat 1 -CooldownMs $CooldownMs -SaveBest -SaveFormat"
