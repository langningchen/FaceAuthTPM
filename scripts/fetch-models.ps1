# SPDX-License-Identifier: GPL-3.0-only
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$modelDir = Join-Path $root 'models'
New-Item -ItemType Directory -Force -Path $modelDir | Out-Null

$files = @(
    @{
        Name='face_detection_yunet_2023mar.onnx'
        Url='https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx'
        Sha256='8F2383E4DD3CFBB4553EA8718107FC0423210DC964F9F4280604804ED2552FA4'
    },
    @{
        Name='face_recognition_sface_2021dec.onnx'
        Url='https://github.com/opencv/opencv_zoo/raw/main/models/face_recognition_sface/face_recognition_sface_2021dec.onnx'
        Sha256='0BA9FBFA01B5270C96627C4EF784DA859931E02F04419C829E83484087C34E79'
    }
)
foreach ($f in $files) {
    $dest = Join-Path $modelDir $f.Name
    if (Test-Path $dest) {
        $hash=(Get-FileHash -Algorithm SHA256 $dest).Hash
        if ($hash -eq $f.Sha256) { Write-Host "$($f.Name): already present and verified"; continue }
        Remove-Item -Force $dest
    }
    Write-Host "Downloading $($f.Name)..."
    Invoke-WebRequest -UseBasicParsing -Uri $f.Url -OutFile $dest
    $hash=(Get-FileHash -Algorithm SHA256 $dest).Hash
    if ($hash -ne $f.Sha256) { Remove-Item -Force $dest; throw "SHA256 mismatch for $($f.Name): $hash" }
    Write-Host "$($f.Name): verified"
}

