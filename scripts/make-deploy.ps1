# SPDX-License-Identifier: GPL-3.0-only
param(
    [string]$Destination,
    [switch]$AllowVmTestBuild
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root 'out\Release'
if(-not $Destination){$Destination=Join-Path $root 'deploy\FaceAuthTPM'}

if(-not (Test-Path (Join-Path $build 'FaceAuthCredentialProvider.dll'))){
    throw 'Production build output not found. Run scripts\build.ps1 first.'
}
if((Test-Path (Join-Path $build 'VM_TEST_BUILD_DO_NOT_INSTALL_ON_HOST.txt')) -and -not $AllowVmTestBuild){
    throw 'Refusing to package an RDP-enabled VM test build for normal deployment.'
}
foreach($m in @('face_detection_yunet_2023mar.onnx','face_recognition_sface_2021dec.onnx')){
    if(-not (Test-Path (Join-Path $root "models\$m"))){ throw "Missing model $m; run scripts\fetch-models.ps1" }
}

if(Test-Path -LiteralPath $Destination){Remove-Item -LiteralPath $Destination -Recurse -Force}
New-Item -ItemType Directory -Force (Join-Path $Destination 'scripts'),(Join-Path $Destination 'models'),(Join-Path $Destination 'out\Release') | Out-Null
Copy-Item -Force (Join-Path $root 'scripts\*.ps1') (Join-Path $Destination 'scripts')
Copy-Item -Force (Join-Path $root 'models\*.onnx') (Join-Path $Destination 'models')
Get-ChildItem -LiteralPath $build -File | Where-Object {
    $_.Extension -in @('.exe','.dll') -or $_.Name -eq 'VM_TEST_BUILD_DO_NOT_INSTALL_ON_HOST.txt'
} | Copy-Item -Force -Destination (Join-Path $Destination 'out\Release')
Copy-Item -Force (Join-Path $root 'VERSION.txt') $Destination
Write-Host "Deployment tree created: $Destination" -ForegroundColor Green
