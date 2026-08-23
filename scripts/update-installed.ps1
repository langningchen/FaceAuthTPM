#Requires -RunAsAdministrator
# SPDX-License-Identifier: GPL-3.0-only
param(
    [string]$BuildDir,
    [switch]$AllowVmTestBuild
)
$ErrorActionPreference='Stop'

$root=Split-Path -Parent $PSScriptRoot
if(-not $BuildDir){$BuildDir=Join-Path $root 'out\Release'}


# FaceAuth 0.5 removes the ICredentialProviderSetUserArray dependency. Existing
# 0.4.x enrollments therefore need a tiny identity metadata file before we swap
# the provider DLL. Keep the old provider fully enabled during this migration.
$profileDir='C:\ProgramData\FaceAuth\profiles'
$credDir='C:\ProgramData\FaceAuth\credentials'
$identityDir='C:\ProgramData\FaceAuth\identities'
$profiles=@(Get-ChildItem $profileDir -Filter '*.fap' -ErrorAction SilentlyContinue)
$creds=@(Get-ChildItem $credDir -Filter '*.fav' -ErrorAction SilentlyContinue)
$credSids=@{}; foreach($c in $creds){$credSids[$c.BaseName]=$true}
$identitySids=@{}; foreach($i in @(Get-ChildItem $identityDir -Filter '*.fai' -ErrorAction SilentlyContinue)){$identitySids[$i.BaseName]=$true}
$missingIdentity=@($profiles | Where-Object {$credSids.ContainsKey($_.BaseName) -and -not $identitySids.ContainsKey($_.BaseName)} | ForEach-Object {$_.BaseName})
if($missingIdentity.Count -gt 0){
    $newEnroll=Join-Path $BuildDir 'FaceAuthEnroll.exe'
    if(-not(Test-Path $newEnroll)){throw "Build output missing: $newEnroll"}
    $install='C:\Program Files\FaceAuth'
    Copy-Item -Force $newEnroll (Join-Path $install 'FaceAuthEnroll.exe')
    Write-Host ''
    Write-Host 'FaceAuth 0.5 identity migration is required before replacing the Provider.' -ForegroundColor Yellow
    Write-Host 'The existing 0.4.x Credential Provider has NOT been disabled or replaced.' -ForegroundColor Green
    Write-Host 'Missing identity metadata for:' -ForegroundColor Yellow
    $missingIdentity | ForEach-Object { Write-Host "  $_" }
    Write-Host ''
    Write-Host 'Log in as each listed Microsoft-account user and run:' -ForegroundColor Cyan
    Write-Host '  & "C:\Program Files\FaceAuth\FaceAuthEnroll.exe" identity'
    Write-Host 'Check that Qualified name is MicrosoftAccount\your-email-address.'
    Write-Host 'If it is not exact, rerun:'
    Write-Host '  & "C:\Program Files\FaceAuth\FaceAuthEnroll.exe" identity --qualified "MicrosoftAccount\you@example.com"'
    Write-Host 'After all listed SIDs have .fai metadata, rerun update-installed.ps1.' -ForegroundColor Cyan
    return
}

function Get-FaceAuthModuleUsers {
    $lines=& "$env:SystemRoot\System32\tasklist.exe" /m FaceAuthCredentialProvider.dll /fo csv /nh 2>$null
    if(-not $lines){ return @() }
    return @($lines | Where-Object {
        $_ -and $_ -notmatch '^INFO:' -and $_ -match 'FaceAuthCredentialProvider\.dll'
    })
}

Write-Host 'Disabling FaceAuth registration before replacing binaries...' -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'disable-provider.ps1')
Start-Sleep -Milliseconds 800

$service=Get-Service -Name 'FaceAuthModel' -ErrorAction SilentlyContinue
if($service -and $service.Status -ne 'Stopped'){
    Write-Host 'Stopping FaceAuth model preloader before replacing OpenCV binaries...' -ForegroundColor Cyan
    Stop-Service -Name 'FaceAuthModel' -Force
    $service.WaitForStatus('Stopped',[TimeSpan]::FromSeconds(10))
}

$sensor=Get-Process -Name FaceAuthSensor -ErrorAction SilentlyContinue
if($sensor){
    throw 'FaceAuthSensor.exe is still running. Return to the unlocked desktop and retry; do not force-kill LogonUI.'
}

$users=Get-FaceAuthModuleUsers
if($users.Count -gt 0){
    Write-Host ''
    Write-Host 'FaceAuthCredentialProvider.dll is still loaded by:' -ForegroundColor Yellow
    $users | ForEach-Object { Write-Host "  $_" }
    throw @'
Refusing to overwrite a loaded Credential Provider DLL. Do NOT kill LogonUI.
Return to an unlocked desktop and close any credential/UAC prompts, then retry.
If the DLL remains loaded, reboot once with FaceAuth disabled, then run install.ps1 and enable-provider.ps1.
'@
}

Write-Host 'Credential Provider DLL is not loaded; installing the new build...' -ForegroundColor Cyan
$installArgs=@{ BuildDir=$BuildDir }
if($AllowVmTestBuild){$installArgs.AllowVmTestBuild=$true}
& (Join-Path $PSScriptRoot 'install.ps1') @installArgs

Write-Host 'Re-enabling FaceAuth...' -ForegroundColor Cyan
if($AllowVmTestBuild){
    & (Join-Path $PSScriptRoot 'enable-provider.ps1') -AllowVmTestBuild
}else{
    & (Join-Path $PSScriptRoot 'enable-provider.ps1')
}

Write-Host 'Update complete. No reboot was required for the file replacement.' -ForegroundColor Green
Write-Host 'Use Win+L for the first post-update test. If anything looks wrong, unlock with Windows Hello/Password and run disable-provider.ps1.'
