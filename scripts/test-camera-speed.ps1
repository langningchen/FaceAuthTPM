# SPDX-License-Identifier: GPL-3.0-only
param(
    [int]$Camera=0,
    [int]$Repeat=1,
    [ValidateSet('all','msmf','dshow','any')][string]$Backend='all',
    [int]$Width=0,
    [int]$Height=0,
    [int]$Fps=0,
    [int]$CooldownMs=500,
    [switch]$SaveBest,
    [switch]$SaveFormat,
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
if(($Width -gt 0) -xor ($Height -gt 0)){throw 'Specify both -Width and -Height, or neither.'}
$args=@('--camera',"$Camera",'--repeat',"$Repeat",'--backend',$Backend,'--cooldown-ms',"$CooldownMs")
if($Width -gt 0){$args+=@('--width',"$Width",'--height',"$Height")}
if($Fps -gt 0){$args+=@('--fps',"$Fps")}
if($SaveBest){$args+='--save-best'}
if($SaveFormat){$args+='--save-format'}
& $Exe @args
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
