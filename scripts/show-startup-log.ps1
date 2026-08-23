# SPDX-License-Identifier: GPL-3.0-only
param([int]$Tail=50)
$ErrorActionPreference='Stop'
$logs=@(
    @{Name='Model preloader (boot-time model load)'; Path='C:\ProgramData\FaceAuth\model-service.log'},
    @{Name='Credential Provider (when LogonUI activates FaceAuth)'; Path='C:\ProgramData\FaceAuth\provider-startup.log'},
    @{Name='Sensor / camera'; Path='C:\ProgramData\FaceAuth\sensor-startup.log'}
)
foreach($item in $logs){
    Write-Host ("=== {0} ===" -f $item.Name) -ForegroundColor Cyan
    if(Test-Path -LiteralPath $item.Path){Get-Content -LiteralPath $item.Path -Tail $Tail}else{Write-Host '(no log yet)' -ForegroundColor Yellow}
    Write-Host ''
}
